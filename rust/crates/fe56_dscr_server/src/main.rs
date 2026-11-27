use fe56_dscr_protocol::message;

use futures_channel::mpsc::unbounded;
use futures_channel::mpsc::UnboundedReceiver;
use futures_channel::mpsc::UnboundedSender;

use futures_util::StreamExt;
use futures_util::TryStreamExt;
use futures_util::future;
use futures_util::pin_mut;

use std::collections::HashMap;
use std::collections::VecDeque;
use std::error::Error;
use std::env;
use std::net::SocketAddr;
use std::str::FromStr;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::SystemTime;

use tokio::time;
use tokio::net::TcpListener;
use tokio::net::TcpStream;

use tokio_tungstenite::tungstenite::Bytes;

// A DSCR server implementation
// polymeric 2026

// Based off tokio-tungstenite's chat server example
// https://github.com/snapview/tokio-tungstenite/blob/master/examples/server.rs

struct HistoricalMessage {
    call_sign: message::IntT,
    sequence_num: message::IntT,
    message: Vec<message::IntT>,
    timestamp: SystemTime,
}

struct ClientFront {
    call_sign: Option<message::IntT>,
    tobox: UnboundedSender<tokio_tungstenite::tungstenite::Message>,
    ping_outstanding_cycles: u8,
}

struct GlobalState {
    client_fronts: HashMap<SocketAddr, ClientFront>,
    peers: HashMap<message::IntT, SocketAddr>,
    msgs: VecDeque<HistoricalMessage>,
    seq: i64,
}

struct ClientBack {
    inbox: UnboundedReceiver<tokio_tungstenite::tungstenite::Message>,
}

async fn handle_connection(global_state: Arc<Mutex<GlobalState>>, raw_stream: TcpStream, addr: SocketAddr) {
    println!("Incoming TCP connection from: {}", addr);

    let ws_stream = tokio_tungstenite::accept_async(raw_stream)
        .await
        .expect("Error during the websocket handshake occurred");
    println!("WebSocket connection established: {}", addr);

    // Insert the write part of this peer to global state.
    let (tx, rx) = unbounded();
    global_state.lock().unwrap().client_fronts.insert(addr, ClientFront {
        call_sign: None,
        tobox: tx,
        ping_outstanding_cycles: 0,
    });

    let (outgoing, incoming) = ws_stream.split();

    let client_back = ClientBack {
        inbox: rx,
    };

    let broadcast_incoming = incoming.try_for_each(|msg| {
        let now_system =  SystemTime::now();
        println!("Received a message from {}: {:?}", addr, msg);
        // tungstenite will implicitly respond to pings
        if msg.is_pong() {
            let mut global_state= global_state.lock().unwrap();
            global_state.client_fronts.entry(addr).and_modify(|x| x.ping_outstanding_cycles = 0);
        } else if msg.is_text() {
            let dscr_message = message::Message::from_str(&msg.into_text().unwrap());
            if let Ok(dscr_message) = dscr_message {
                match dscr_message {
                    message::Message::SetCallSign { call_sign, reconnect } => {
                        let mut global_state= global_state.lock().unwrap();
                        let never_registered = global_state.client_fronts[&addr].call_sign.is_none();
                        if reconnect && never_registered {
                            // TODO what happens if we evict somebody's connection?
                            let old_entry = global_state.peers.insert(call_sign, addr);
                            if let Some(old_peer) = old_entry {
                                global_state.client_fronts.entry(old_peer).and_modify(|x| x.call_sign = None);
                            }
                            global_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                            global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::ReconnectOk.to_string())).unwrap();
                        } else if global_state.peers.contains_key(&call_sign) {
                            if global_state.client_fronts[&addr].call_sign == Some(call_sign) {
                                global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string())).unwrap();
                            } else {
                                global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::CallSignInUse(call_sign).to_string())).unwrap();
                            }
                        } else {
                            if let Some(call_sign) = global_state.client_fronts[&addr].call_sign {
                                global_state.peers.remove(&call_sign);
                            }
                            global_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                            global_state.peers.insert(call_sign, addr);
                            global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string())).unwrap();
                            let mut call_signs_sorted = global_state.peers.keys().copied().collect::<Vec<message::IntT>>();
                            call_signs_sorted.sort();
                            for (_peer_call_sign, peer_socket) in &global_state.peers {
                                let peer_tobox = &global_state.client_fronts[&peer_socket];
                                peer_tobox.tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string())).unwrap();
                            }
                            if never_registered {
                                for hmsg in &global_state.msgs {
                                    global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::ReceivedMessage { call_sign: hmsg.call_sign, sequence_num: hmsg.sequence_num, message: hmsg.message.clone() }.to_string())).unwrap();
                                }
                            }
                        }
                    },
                    message::Message::BroadcastMessage(items) => {
                        let mut global_state= global_state.lock().unwrap();
                        if let Some(call_sign) = global_state.client_fronts[&addr].call_sign {
                            if items.len() <= 2000 {
                                let h_message = HistoricalMessage { call_sign, sequence_num: global_state.seq, message: items.clone(), timestamp: now_system };
                                if global_state.msgs.len() >= 10 {
                                    global_state.msgs.pop_front();
                                }
                                global_state.msgs.push_back(h_message);
                                let r_message = message::Message::ReceivedMessage { call_sign, sequence_num: global_state.seq, message: items };
                                global_state.seq = (global_state.seq + 1) % 512;
                                for (_peer_call_sign, peer_socket) in &global_state.peers {
                                    let peer_tobox = &global_state.client_fronts[&peer_socket];
                                    peer_tobox.tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(r_message.clone().to_string())).unwrap();
                                }
                            }
                        }
                    },
                    _ => {}
                }
            }
        }

        future::ok(())
    });

    let receive_from_others = client_back.inbox.map(Ok).forward(outgoing);

    let ping_watchdog = async {
        let mut interval = time::interval(Duration::from_secs(15));
        loop {
            interval.tick().await;
            let mut global_state= global_state.lock().unwrap();
            if global_state.client_fronts[&addr].ping_outstanding_cycles == 2 {
                return;
            } else if global_state.client_fronts[&addr].ping_outstanding_cycles == 0 {
                global_state.client_fronts[&addr].tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::Ping(Bytes::default())).unwrap();
                global_state.client_fronts.entry(addr).and_modify(|x| x.ping_outstanding_cycles = 1);
            } else {
                global_state.client_fronts.entry(addr).and_modify(|x| x.ping_outstanding_cycles += 1);
            }
        }
    };

    pin_mut!(broadcast_incoming, receive_from_others);
    tokio::select! {
        _ = broadcast_incoming => {},
        _ = receive_from_others => {},
        _ = ping_watchdog => {},
    }

    println!("{} disconnected", &addr);
    {
        let mut global_state= global_state.lock().unwrap();
        let old_tobox = global_state.client_fronts.remove(&addr);
        if let Some(tobox) = old_tobox && let Some(call_sign) = tobox.call_sign {
            global_state.peers.remove(&call_sign);
        }
        let mut call_signs_sorted = global_state.peers.keys().copied().collect::<Vec<message::IntT>>();
        call_signs_sorted.sort();
        for (_peer_call_sign, peer_socket) in &global_state.peers {
            let peer_tobox = &global_state.client_fronts[&peer_socket];
            peer_tobox.tobox.unbounded_send(tokio_tungstenite::tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string())).unwrap();
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let addr = env::args().nth(1).unwrap_or_else(|| "127.0.0.1:8080".to_string());

    // Create the event loop and TCP listener we'll accept connections on.
    let try_socket = TcpListener::bind(&addr).await;
    let listener = try_socket.expect("Failed to bind");
    println!("Listening on: {}", addr);

    let global_state = Arc::new(Mutex::new(GlobalState {
        client_fronts: HashMap::new(),
        peers: HashMap::new(),
        msgs: VecDeque::new(),
        seq: 0,
    }));

    // Spawn the handling of each connection in a separate task.
    while let Ok((stream, addr)) = listener.accept().await {
        tokio::spawn(handle_connection(global_state.clone(), stream, addr));
    }

    Ok(())
}
