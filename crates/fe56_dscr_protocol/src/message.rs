use std::fmt;
use std::str;

// An implementation of the DSCR message protocol.
// https://github.com/dixonary/mfds-server/tree/main

pub type IntT = i64;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Message {
    // Client -> Server
    // S
    SetCallSign { call_sign: IntT, reconnect: bool },
    // M
    BroadcastMessage(Vec<IntT>),

    // Server -> Client
    // R
    ReceivedMessage { call_sign: IntT, sequence_num: IntT, message: Vec<IntT> },
    // K
    CallSignOk(IntT),
    // E
    ReconnectOk,
    // U
    CallSignInUse(IntT),
    // C
    CallSignsUpdate(Vec<IntT>),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageParseErrorKind {
    UnrecognizedSignifier,
    UnexpectedSymbolValue,
    TooFewSymbols,
    TooManySymbols,
}

impl str::FromStr for Message {
    type Err = MessageParseErrorKind;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // We chunk the message string as a comma delimited list of strings.
        // Message strings normatively follow the pattern /^[A-Z](,-?[0-9]+)*$/.

        // The first list element is matched by string comparison, and subsequent elements are parsed as signed integers.

        let mut it = s.split(',');
        match it.next() {
            Some("S") => {
                // S,<callsign> ; S,<callsign>,0
                // Callsign is required
                let call_sign = it
                    .next().ok_or(Self::Err::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // Reconnect hint is optional but if it is present, it must be 0
                let reconnect = match it.next() {
                    Some(hint) => match hint.parse::<IntT>() {
                        Ok(0) => true,
                        _ => false
                        // pedantic
                        // _ => return Err(Self::Err::UnexpectedSymbolValue)
                    },
                    None => false
                };

                // pedantic: Reject if some tokens are unconsumed
                // if it.next().is_some() {
                //     return Err(Self::Err::TooManySymbols);
                // }

                Ok(Self::SetCallSign { call_sign, reconnect })
            },
            Some("M") => {
                // M,<symbol>[,<symbol>...]
                let message: Vec<IntT> = it
                    .map(|x| x.parse::<IntT>()).collect::<Result<Vec<_>, _>>()
                    .map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // pedantic: Reject an empty message
                // if message.is_empty() {
                //     return Err(Self::Err::TooFewSymbols);
                // }

                Ok(Self::BroadcastMessage(message))
            },

            Some("R") => {
                // R,<callsign>,<sequence>,<symbol>[,<symbol>...]
                // Callsign is required
                let call_sign = it
                    .next().ok_or(Self::Err::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // Sequence is required
                let sequence_num = it
                    .next().ok_or(Self::Err::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                let message: Vec<IntT> = it
                    .map(|x| x.parse::<IntT>()).collect::<Result<Vec<_>, _>>()
                    .map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // pedantic: Reject an empty message
                // if message.is_empty() {
                //     return Err(Self::Err::TooFewSymbols);
                // }

                Ok(Self::ReceivedMessage { call_sign, sequence_num, message })
            },
            Some("K") => {
                // K,<callsign>
                // Callsign is required
                let call_sign = it
                    .next().ok_or(Self::Err::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // pedantic: Reject if some tokens are unconsumed
                // if it.next().is_some() {
                //     return Err(Self::Err::TooManySymbols);
                // }

                Ok(Self::CallSignOk(call_sign))
            },
            Some("E") => {
                // pedantic: Reject if some tokens are unconsumed
                // if it.next().is_some() {
                //     return Err(Self::Err::TooManySymbols);
                // }

                Ok(Self::ReconnectOk)
            },
            Some("U") => {
                // U,<callsign>
                // Callsign is required
                let call_sign = it
                    .next().ok_or(Self::Err::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                // pedantic: Reject if some tokens are unconsumed
                // if it.next().is_some() {
                //     return Err(Self::Err::TooManySymbols);
                // }

                Ok(Self::CallSignInUse(call_sign))
            },
            Some("C") => {
                // C[,<callsign>...]
                let call_signs: Vec<IntT> = it
                    .map(|x| x.parse::<IntT>()).collect::<Result<Vec<_>, _>>()
                    .map_err(|_| Self::Err::UnexpectedSymbolValue)?;

                Ok(Self::CallSignsUpdate(call_signs))
            },

            // Empty message string or unknown signifier
            Some(..) => {
                Err(Self::Err::UnrecognizedSignifier)
            },

            // split should produce 1 token with an empty string
            None => {
                unreachable!()
            }
        }
    }

}

impl fmt::Display for Message {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::SetCallSign { call_sign, reconnect } => {
                write!(f, "S,{}", call_sign)?;
                if *reconnect {
                    write!(f, ",{}", 0)?;
                }
            },
            Self::BroadcastMessage(message) => {
                write!(f, "M")?;
                for symbol in message {
                    write!(f, ",{}", symbol)?;
                }
            },

            Self::ReceivedMessage { call_sign, sequence_num, message } => {
                write!(f, "R,{},{}", call_sign, sequence_num)?;
                for symbol in message {
                    write!(f, ",{}", symbol)?;
                }
            },
            Self::CallSignOk(call_sign) => {
                write!(f, "K,{}", call_sign)?;
            },
            Self::ReconnectOk => {
                write!(f, "E")?;
            },
            Self::CallSignInUse(call_sign) => {
                write!(f, "U,{}", call_sign)?;
            },
            Self::CallSignsUpdate(call_signs) => {
                write!(f, "C")?;
                for call_sign in call_signs {
                    write!(f, ",{}", call_sign)?;
                }
            }
        }
        Ok(())
    }
}
