use crate::message;
use std::time;

// A client session state machine to control interactions with a DSCR server.

// Websocket liveness tracker
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClientTransportState {
    Unconnected { retry: Option<time::Instant> },
    Connecting { check: time::Instant },
    Connected { since: time::Instant, last_rx: time::Instant },
}

// Registration tracker
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClientApplicationState {
    Unregistered { retry: Option<time::Instant> },
    Registering { check: time::Instant },
    Registered { call_sign: message::SymbolT },
}

#[derive(Debug)]
pub struct ClientConfig {
    url: Option<String>,
    desired_call_sign: Option<message::SymbolT>
}

#[derive(Debug)]
pub struct ClientApplication {
    state: ClientApplicationState,
    requested_call_sign: Option<message::SymbolT>,
    call_signs: Vec<message::SymbolT>,
}

#[derive(Debug)]
pub struct ClientTransport {
    state: ClientTransportState,
    connection_consecutive_failure_count: u32,
    connection_initiation_time: Option<time::Instant>,
}

#[derive(Debug)]
pub struct ClientSession {
    config: ClientConfig,
    transport: ClientTransport,
    application: ClientApplication,
    active: bool,
    outbox_frontend: Vec<FrontendAction>,
    outbox_backend: Vec<BackendAction>,
}

#[derive(Debug)]
pub enum FrontendAction {
    Message { rx_time: time::Instant, call_sign: message::SymbolT, sequence_num: message::SymbolT, message: Vec<message::SymbolT> },
    CallSignsUpdate,
}

#[derive(Debug)]
pub enum BackendAction {
    Connect,
    Disconnect,
    SendText { s: String },
    // SendPing,
}

fn exp_backoff_schedule(retry_count: u32) -> time::Duration {
    match retry_count {
        0 => time::Duration::from_millis(0),
        1 => time::Duration::from_millis(100),
        2 => time::Duration::from_millis(200),
        3 => time::Duration::from_millis(400),
        4 => time::Duration::from_millis(800),
        5 => time::Duration::from_millis(1600),
        6 => time::Duration::from_millis(3200),
        _ => time::Duration::from_millis(6400),
    }
}

impl ClientSession {
    pub fn new() -> Self {
        Self {
            config: ClientConfig {
                url: None,
                desired_call_sign: None,
            },
            transport: ClientTransport {
                state: ClientTransportState::Unconnected {
                    retry: None,
                },
                connection_consecutive_failure_count: 0,
                connection_initiation_time: None
            },
            application: ClientApplication {
                state: ClientApplicationState::Unregistered {
                    retry: None,
                },
                requested_call_sign: None,
                call_signs: vec![],
            },
            active: false,
            outbox_frontend: vec![],
            outbox_backend: vec![],
        }
    }

    // pub fn pump_realtime_task(&mut self, t: time::Instant) {
    //     if self.active {
    //         if !matches!(self.transport.state, ClientTransportState::Connected { .. }) {
    //             // Reconnect

    //         } else if !matches!(self.application.state, ClientApplicationState::Registered { .. }) {
    //             // Register
    //         }
    //     }
    // }

    pub fn backend_put_connecting(&mut self, t: time::Instant) {
        self.transport.state = ClientTransportState::Connecting {
            check: t + time::Duration::from_secs(5),
        };
        self.application.state = ClientApplicationState::Unregistered {
            retry: None,
        };
        self.transport.connection_initiation_time = Some(t);
    }
    pub fn backend_put_connected(&mut self, t: time::Instant) {
        self.transport.state = ClientTransportState::Connected {
            since: t,
            last_rx: t,
        };
        self.transport.connection_consecutive_failure_count = 0;
    }
    pub fn backend_put_connect_fail(&mut self, t: time::Instant) {
        self.transport.connection_consecutive_failure_count = self.transport.connection_consecutive_failure_count.saturating_add(1);
        self.transport.state = ClientTransportState::Unconnected {
            retry: Some(t + exp_backoff_schedule(self.backend_get_connection_consecutive_failure_count())),
        };
        self.transport.connection_initiation_time = None;
    }
    pub fn backend_put_disconnected(&mut self, t: time::Instant) {
        self.transport.state = ClientTransportState::Unconnected {
            retry: None,
        };
        self.transport.connection_initiation_time = None;
    }
    pub fn backend_put_ping(&mut self, t: time::Instant) {
        if let ClientTransportState::Connected { last_rx, .. } = &mut self.transport.state {
            *last_rx = t;
        } else {
            debug_assert!(false, "Backend must not push pings when transport is down.");
        }
    }
    pub fn backend_put_text(&mut self, t: time::Instant, s: &str) {
        if let ClientTransportState::Connected { last_rx, .. } = &mut self.transport.state {
            *last_rx = t;
        } else {
            debug_assert!(false, "Backend must not push text when transport is down.");
        }

        let message = s.parse::<message::Message>();
        match message {
            Ok(message::Message::ReceivedMessage { call_sign, sequence_num, message }) => {
                // Forward a message to the frontend
                self.outbox_frontend.push(FrontendAction::Message { rx_time: t, call_sign, sequence_num, message });
            },
            Ok(message::Message::CallSignOk(call_sign)) => {
                // Server ack'd our registration request.
                // TODO verify what we asked for and permanently disconnect if mismatched
                self.application.state = ClientApplicationState::Registered { call_sign };
            },
            Ok(message::Message::ReconnectOk) => {
                // Server ack'd our reconnect registration request.
                if let Some(call_sign) = self.application.requested_call_sign {
                    self.application.state = ClientApplicationState::Registered { call_sign };
                    self.application.requested_call_sign = None;
                } else {
                    // TODO permanently disconnect.
                }
            },
            Ok(message::Message::CallSignInUse(_call_sign)) => {
                // Server n'ack'd our registration request.
                self.application.state = ClientApplicationState::Unregistered {
                    retry: Some(t + time::Duration::from_secs(2)),
                };
            },
            Ok(message::Message::CallSignsUpdate(call_signs)) => {
                // Inform the frontend of a callsign list update
                self.application.call_signs = call_signs;
                self.outbox_frontend.push(FrontendAction::CallSignsUpdate);
            },
            _ => {
                // TODO log?
            }
        }
    }
    pub fn backend_get_connection_consecutive_failure_count(&mut self) -> u32 {
        self.transport.connection_consecutive_failure_count
    }

    pub fn frontend_put_callsign_registration(&mut self, t: time::Instant, call_sign: message::SymbolT, reconnect: bool) {
        self.application.state = ClientApplicationState::Registering {
            check: t + time::Duration::from_secs(5),
        };
        self.application.requested_call_sign = Some(call_sign);
        self.outbox_backend.push(BackendAction::SendText { s: message::Message::SetCallSign { call_sign, reconnect }.to_string() });
    }
    pub fn frontend_put_message(&mut self, t: time::Instant, message: Vec<message::SymbolT>) {
        self.outbox_backend.push(BackendAction::SendText { s: message::Message::BroadcastMessage(message).to_string() });
    }
    pub fn frontend_get_connected(&mut self) -> bool {
        matches!(self.transport.state, ClientTransportState::Connected { .. })
    }
    pub fn frontend_get_registered(&mut self) -> bool {
        matches!(self.transport.state, ClientTransportState::Connected { .. }) &&
        matches!(self.application.state, ClientApplicationState::Registered { .. })
    }
    pub fn frontend_get_events(&mut self) {}
}
