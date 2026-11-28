use fe56_dscr_protocol::message;

use clap::Parser;
use clap::ValueEnum;

use futures_util::SinkExt;
use futures_util::StreamExt;

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::collections::VecDeque;
use std::error::Error;
use std::net::SocketAddr;
use std::time::Duration;

use tokio::time;
use tokio::net::TcpListener;
use tokio::net::TcpStream;
use tokio::signal;
use tokio::sync::mpsc;

use tokio_tungstenite::tungstenite;

// A DSCR server implementation
// polymeric 2026

// Architecture:
//
// A Coordinator task manages the global list of DSCR peers and is responsible for:
// - tracking the callsign to socket map
// - issuing DSCR messages to each Connection
//
// Connection tasks represent each connection instance to the server, which manage:
// - decoding DSCR messages within incoming WebSocket messages
// - issuing commands to the Coordinator
// - issuing outgoing WebSocket messages
// - handling generic WebSocket lifecycle and control messages

const MAX_MESSAGE_HISTORY: usize = 10;
const MAX_MESSAGE_SYMBOLS: usize = 2000;
const MAX_MESSAGE_SEQ: i64 = 512;
const PING_WATCHDOG_INTERVAL_SEC: u64 = 15;
const PING_WATCHDOG_FAIL_CYCLES: u64 = 2;

struct ClientFront {
    call_sign: Option<message::IntT>,
    tobox: mpsc::UnboundedSender<tungstenite::Message>,
}

struct ClientBack {
    inbox: mpsc::UnboundedReceiver<tungstenite::Message>,
    ping_outstanding_cycles: u64,
}

struct CoordinatorState {
    client_fronts: HashMap<SocketAddr, ClientFront>,
    peers: BTreeMap<message::IntT, SocketAddr>,
    msgs: VecDeque<message::Message>,
    seq: i64,
}

enum CoordinatorCommand {
    ConnectionUp { addr: SocketAddr, tobox: mpsc::UnboundedSender<tungstenite::Message> },
    ConnectionDown { addr: SocketAddr },
    SetCallSign { addr: SocketAddr, call_sign: message::IntT, reconnect: bool },
    BroadcastMessage { addr: SocketAddr, items: Vec<message::IntT> },
}

async fn coordinator_task(mut inbox: mpsc::UnboundedReceiver<CoordinatorCommand>) {
    let mut coord_state = CoordinatorState {
        client_fronts: HashMap::new(),
        peers: BTreeMap::new(),
        msgs: VecDeque::new(),
        seq: 0,
    };

    while let Some(cmd) = inbox.recv().await {
        match cmd {
            CoordinatorCommand::ConnectionUp { addr, tobox } => {
                coord_state.client_fronts.insert(addr, ClientFront {
                    call_sign: None,
                    tobox,
                });
            },
            CoordinatorCommand::ConnectionDown { addr } => {
                let old_tobox = coord_state.client_fronts.remove(&addr);
                if let Some(tobox) = old_tobox && let Some(call_sign) = tobox.call_sign {
                    coord_state.peers.remove(&call_sign);
                }
                let call_signs_sorted = coord_state.peers.keys().copied().collect::<Vec<message::IntT>>();
                for (_peer_call_sign, peer_socket) in &coord_state.peers {
                    if let Some(peer) = coord_state.client_fronts.get(peer_socket) {
                        let _ = peer.tobox.send(tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string()));
                    }
                }
            },
            CoordinatorCommand::SetCallSign { addr, call_sign, reconnect } => {
                if let Some(sender) = coord_state.client_fronts.get(&addr) {
                    let sender_call_sign = sender.call_sign;
                    let sender_tobox = sender.tobox.clone();
                    let never_registered = sender_call_sign.is_none();
                    if reconnect && never_registered {
                        let old_entry = coord_state.peers.insert(call_sign, addr);
                        if let Some(old_peer) = old_entry {
                            coord_state.client_fronts.entry(old_peer).and_modify(|x| x.call_sign = None);
                        }
                        coord_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                        let _ = sender_tobox.send(tungstenite::Message::text(message::Message::ReconnectOk.to_string()));
                    } else if coord_state.peers.contains_key(&call_sign) {
                        if sender_call_sign == Some(call_sign) {
                            let _ = sender_tobox.send(tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string()));
                        } else {
                            let _ = sender_tobox.send(tungstenite::Message::text(message::Message::CallSignInUse(call_sign).to_string()));
                        }
                    } else {
                        if let Some(call_sign) = sender_call_sign {
                            coord_state.peers.remove(&call_sign);
                        }
                        coord_state.client_fronts.entry(addr).and_modify(|x| x.call_sign = Some(call_sign));
                        coord_state.peers.insert(call_sign, addr);
                        let _ = sender_tobox.send(tungstenite::Message::text(message::Message::CallSignOk(call_sign).to_string()));
                        let call_signs_sorted = coord_state.peers.keys().copied().collect::<Vec<message::IntT>>();
                        for (_peer_call_sign, peer_socket) in &coord_state.peers {
                            if let Some(peer) = coord_state.client_fronts.get(peer_socket) {
                                let _ = peer.tobox.send(tungstenite::Message::text(message::Message::CallSignsUpdate(call_signs_sorted.clone()).to_string()));
                            }
                        }
                        if never_registered {
                            for msg in &coord_state.msgs {
                                let _ = sender_tobox.send(tungstenite::Message::text(msg.to_string()));
                            }
                        }
                    }
                }
            },
            CoordinatorCommand::BroadcastMessage { addr, items } => {
                if let Some(sender) = coord_state.client_fronts.get(&addr) {
                    if let Some(call_sign) = sender.call_sign {
                        if items.len() <= MAX_MESSAGE_SYMBOLS {
                            let r_message = message::Message::ReceivedMessage { call_sign, sequence_num: coord_state.seq, message: items };
                            if coord_state.msgs.len() >= MAX_MESSAGE_HISTORY {
                                coord_state.msgs.pop_front();
                            }
                            coord_state.msgs.push_back(r_message.clone());
                            coord_state.seq = (coord_state.seq + 1) % MAX_MESSAGE_SEQ;
                            for (_peer_call_sign, peer_socket) in &coord_state.peers {
                                if let Some(peer) = coord_state.client_fronts.get(peer_socket) {
                                    let _ = peer.tobox.send(tungstenite::Message::text(r_message.to_string()));
                                }
                            }
                        }
                    }
                }
            },
        }
    }
}

async fn handle_connection(quirks: &message::MessageParseQuirks, coordinator_tobox: mpsc::UnboundedSender<CoordinatorCommand>, raw_stream: TcpStream, addr: SocketAddr) {
    println!("Incoming TCP connection from: {}", addr);

    let ws_stream = tokio_tungstenite::accept_async(raw_stream)
        .await
        .expect("Error during WebSocket handshake occurred");
    println!("WebSocket connection established: {}", addr);

    // Create a channel with which the coordinator may send WebSockets messages to the connection
    let (tobox, inbox) = mpsc::unbounded_channel();
    coordinator_tobox.send(CoordinatorCommand::ConnectionUp { addr, tobox: tobox.clone() }).expect("Error issuing command to coordinator");

    let mut client_back = ClientBack {
        inbox,
        ping_outstanding_cycles: 0,
    };

    // Split the WebSocket stream into a tx/rx pair
    let (mut outgoing, mut incoming) = ws_stream.split();

    // Connect the coordinator channel to the WebSocket tx channel
    let receive_from_others = async {
        while let Some(msg) = client_back.inbox.recv().await {
            if outgoing.send(msg).await.is_err() {
                break;
            }
        }
    };

    let mut ping_watchdog_interval = time::interval(Duration::from_secs(PING_WATCHDOG_INTERVAL_SEC));

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
                        let dscr_message = message::Message::from_str_with_quirks(&msg.into_text().unwrap(), quirks);
                        if let Ok(dscr_message) = dscr_message {
                            match dscr_message {
                                message::Message::SetCallSign { call_sign, reconnect } => {
                                    coordinator_tobox.send(CoordinatorCommand::SetCallSign { addr, call_sign, reconnect }).expect("Error issuing command to coordinator");
                                },
                                message::Message::BroadcastMessage(items) => {
                                    coordinator_tobox.send(CoordinatorCommand::BroadcastMessage { addr, items }).expect("Error issuing command to coordinator");
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
                if client_back.ping_outstanding_cycles == PING_WATCHDOG_FAIL_CYCLES {
                    break;
                } else if client_back.ping_outstanding_cycles == 0 {
                    let _ = tobox.send(tungstenite::Message::Ping("".into()));
                    client_back.ping_outstanding_cycles = 1;
                } else {
                    client_back.ping_outstanding_cycles += 1;
                }
            },
        }
    }

    println!("{} disconnected", &addr);
    coordinator_tobox.send(CoordinatorCommand::ConnectionDown { addr }).expect("Error issuing command to coordinator");
}

#[derive(Debug, Clone, ValueEnum)]
enum ParserPersonality {
    /// Accept more out-of-spec message format deviations, within reason
    Permissive,
    /// Reject most out-of-spec message format deviations, within reason
    Pedantic,
    /// Match Dixonary's server's message parser behaviour
    DixonaryLike
}

#[derive(Debug, Parser)]
#[command(version, about, long_about = None)]
struct Args {
    /// Controls the strictness of the server's message parser
    #[arg(short, long, value_enum, default_value_t = ParserPersonality::DixonaryLike)]
    personality: ParserPersonality,

    /// Sets the bound IP address and TCP port on which to listen for unencrypted WebSockets connections
    #[arg(default_value_t = "127.0.0.1:3101".to_string())]
    bind_addr: String,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let args = Args::parse();

    // Configure the message parser quirks based on the given personality argument
    let quirks = match args.personality {
        ParserPersonality::Permissive => &message::MessageParseQuirks::PERMISSIVE,
        ParserPersonality::Pedantic => &message::MessageParseQuirks::PEDANTIC,
        ParserPersonality::DixonaryLike =>  &message::MessageParseQuirks::DIXONARY_LIKE,
    };

    // Create the event loop and TCP listener we'll accept connections on.
    let try_socket = TcpListener::bind(&args.bind_addr).await;
    let listener = try_socket.expect("Failed to bind");
    println!("Listening on: {}", args.bind_addr);

    let (coordinator_tobox, coordinator_inbox) = mpsc::unbounded_channel();

    let mut coordinator = tokio::spawn(coordinator_task(coordinator_inbox));

    // Spawn the Coordinator and monitor for new connections
    loop {
        tokio::select! {
            maybe_stream = listener.accept() => {
                if let Ok((stream, addr)) = maybe_stream {
                    tokio::spawn(handle_connection(quirks, coordinator_tobox.clone(), stream, addr));
                }
            }
            _ = &mut coordinator => {
                break;
            }
            _ = signal::ctrl_c() => {
                break;
            }
        }
    }

    Ok(())
}
