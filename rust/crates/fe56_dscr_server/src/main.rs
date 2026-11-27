use fe56_dscr_protocol::message;

use futures_util::SinkExt;
use futures_util::StreamExt;

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::collections::VecDeque;
use std::error::Error;
use std::env;
use std::net::SocketAddr;
use std::str::FromStr;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;

use tokio::time;
use tokio::net::TcpListener;
use tokio::net::TcpStream;
use tokio::sync::mpsc;

use tokio_tungstenite::tungstenite;

// A DSCR server implementation
// polymeric 2026

// Based off tokio-tungstenite's chat server example
// https://github.com/snapview/tokio-tungstenite/blob/master/examples/server.rs

struct ClientFront {
    call_sign: Option<message::IntT>,
    tobox: mpsc::UnboundedSender<tungstenite::Message>,
}

struct GlobalState {
    client_fronts: HashMap<SocketAddr, ClientFront>,
    peers: BTreeMap<message::IntT, SocketAddr>,
    msgs: VecDeque<message::Message>,
    seq: i64,
}

struct ClientBack {
    inbox: mpsc::UnboundedReceiver<tungstenite::Message>,
    ping_outstanding_cycles: u8,
}

async fn handle_connection(global_state: Arc<Mutex<GlobalState>>, raw_stream: TcpStream, addr: SocketAddr) {
    println!("Incoming TCP connection from: {}", addr);

    let ws_stream = tokio_tungstenite::accept_async(raw_stream)
        .await
        .expect("Error during WebSocket handshake occurred");
    println!("WebSocket connection established: {}", addr);

    // Insert the write part of this peer to global state.
    let (tx, rx) = mpsc::unbounded_channel();
    global_state.lock().unwrap().client_fronts.insert(addr, ClientFront {
        call_sign: None,
        tobox: tx.clone(),
    });

    let mut client_back = ClientBack {
        inbox: rx,
        ping_outstanding_cycles: 0,
    };

    let (mut outgoing, mut incoming) = ws_stream.split();

    let receive_from_others = async {
        while let Some(msg) = client_back.inbox.recv().await {
            if outgoing.send(msg).await.is_err() {
                break;
            }
        }
    };

    let mut ping_watchdog_interval = time::interval(Duration::from_secs(15));

    tokio::pin!(receive_from_others);

    loop {
        tokio::select! {
            msg_result = incoming.next() => {
                if let Some(msg_result) = msg_result && let Ok(msg) = msg_result {
                    println!("Received a message from {}: {:?}", addr, msg);
                    // tungstenite will implicitly respond to pings
                    if msg.is_pong() {
                        client_back.ping_outstanding_cycles = 0;
                    } else if msg.is_text() {
                        let dscr_message = message::Message::from_str(&msg.into_text().unwrap());
                        if let Ok(dscr_message) = dscr_message {
                            let mut global_state = global_state.lock().unwrap();
                            let maybe_call_sign = global_state.client_fronts[&addr].call_sign;
                            match dscr_message {
                                message::Message::SetCallSign { call_sign, reconnect } => {
                                    let never_registered = maybe_call_sign.is_none();
                                    if reconnect && never_registered {
                                        // TODO what happens if we evict somebody's connection?
                                        let old_entry = global_state.peers.insert(call_sign, addr);
                                        if let Some(old_peer) = old_entry {
                                            global_state.client_fronts.entry(old_peer).and_modify(|x| x.call_sign = None);
                                        }
                                        global_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                                        tx.send(tungstenite::Message::text(message::Message::ReconnectOk.to_string())).unwrap();
                                    } else if global_state.peers.contains_key(&call_sign) {
                                        if maybe_call_sign == Some(call_sign) {
                                            tx.send(tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string())).unwrap();
                                        } else {
                                            tx.send(tungstenite::Message::text(message::Message::CallSignInUse(call_sign).to_string())).unwrap();
                                        }
                                    } else {
                                        if let Some(call_sign) = maybe_call_sign {
                                            global_state.peers.remove(&call_sign);
                                        }
                                        global_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                                        global_state.peers.insert(call_sign, addr);
                                        tx.send(tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string())).unwrap();
                                        let call_signs_sorted = global_state.peers.keys().copied().collect::<Vec<message::IntT>>();
                                        for (_peer_call_sign, peer_socket) in &global_state.peers {
                                            let peer_tobox = &global_state.client_fronts[&peer_socket];
                                            peer_tobox.tobox.send(tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string())).unwrap();
                                        }
                                        if never_registered {
                                            for hmsg in &global_state.msgs {
                                                tx.send(tungstenite::Message::text(hmsg.to_string())).unwrap();
                                            }
                                        }
                                    }
                                },
                                message::Message::BroadcastMessage(items) => {
                                    if let Some(call_sign) = maybe_call_sign {
                                        if items.len() <= 2000 {
                                            let r_message = message::Message::ReceivedMessage { call_sign, sequence_num: global_state.seq, message: items };
                                            if global_state.msgs.len() >= 10 {
                                                global_state.msgs.pop_front();
                                            }
                                            global_state.msgs.push_back(r_message.clone());
                                            global_state.seq = (global_state.seq + 1) % 512;
                                            for (_peer_call_sign, peer_socket) in &global_state.peers {
                                                let peer_tobox = &global_state.client_fronts[&peer_socket];
                                                peer_tobox.tobox.send(tungstenite::Message::text(r_message.to_string())).unwrap();
                                            }
                                        }
                                    }
                                },
                                _ => {}
                            }
                        }
                    }
                } else {
                    break;
                }
            },
            _ = &mut receive_from_others => {
                break;
            },

            _ = ping_watchdog_interval.tick() => {
                if client_back.ping_outstanding_cycles == 2 {
                    break;
                } else if client_back.ping_outstanding_cycles == 0 {
                    tx.send(tungstenite::Message::Ping(tungstenite::Bytes::default())).unwrap();
                    client_back.ping_outstanding_cycles = 1;
                } else {
                    client_back.ping_outstanding_cycles += 1;
                }
            },
        }
    }

    println!("{} disconnected", &addr);
    {
        let mut global_state= global_state.lock().unwrap();
        let old_tobox = global_state.client_fronts.remove(&addr);
        if let Some(tobox) = old_tobox && let Some(call_sign) = tobox.call_sign {
            global_state.peers.remove(&call_sign);
        }
        let call_signs_sorted = global_state.peers.keys().copied().collect::<Vec<message::IntT>>();
        for (_peer_call_sign, peer_socket) in &global_state.peers {
            let peer_tobox = &global_state.client_fronts[&peer_socket];
            peer_tobox.tobox.send(tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string())).unwrap();
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
        peers: BTreeMap::new(),
        msgs: VecDeque::new(),
        seq: 0,
    }));

    // Spawn the handling of each connection in a separate task.
    while let Ok((stream, addr)) = listener.accept().await {
        tokio::spawn(handle_connection(global_state.clone(), stream, addr));
    }

    Ok(())
}
