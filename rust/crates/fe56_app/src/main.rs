use std::time;

use fe56_dscr_protocol::message;
use fe56_dscr_protocol::client;
use futures_util::SinkExt;
use futures_util::StreamExt;
use tokio_tungstenite::tungstenite;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    rustls::crypto::aws_lc_rs::default_provider().install_default().expect("Default TLS provider already set.");
    let mut client = client::ClientSession::new();
    let (mut socket, response) = tokio_tungstenite::connect_async("wss://dscr-relay.dixonary.co.uk").await?;
    println!("HTTP {}", response.status());
    client.backend_put_connecting(time::Instant::now());
    client.backend_put_connected(time::Instant::now());
    let set_call_sign = message::Message::SetCallSign { call_sign: 0o4242, reconnect: false };
    socket.send(tungstenite::Message::text(set_call_sign.to_string())).await?;
    client.frontend_put_callsign_registration(time::Instant::now(), 0o4242, false);
    let sleep = tokio::time::sleep(tokio::time::Duration::from_secs(30));
    tokio::pin!(sleep);
    loop {
        tokio::select! {
            _ = &mut sleep => break,
            msg = socket.next() => match msg {
                Some(Ok(tungstenite::Message::Text(string))) => {
                    println!("{}", string);
                    client.backend_put_text(time::Instant::now(), &string);
                },
                Some(Ok(other)) => {
                    println!("{}", other);
                    client.backend_put_ping(time::Instant::now());
                },
                Some(Err(_)) => client.backend_put_disconnected(time::Instant::now()),
                None => client.backend_put_disconnected(time::Instant::now()),
            },
        }
    }
    println!("{:?}", client);
    Ok(())
}
