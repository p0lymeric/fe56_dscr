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
    TrailingWhitespace,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MessageParseQuirks {
    // Accept messages containing more than the expected number of list elements, if otherwise valid
    pub accept_extra_elements: bool,
    // Accept and strip trailing whitespaces from an otherwise valid message
    pub accept_trailing_whitespace: bool,
    // Accept a nonzero reconnect argument given to S, interpreted as "not a reconnect"
    pub accept_s_nonzero_reconnect_arg: bool,
    // Accept a negative callsign given to S
    pub accept_s_negative_call_sign: bool,
    // Accept the string "M", interpreted as an empty message
    pub accept_m_empty_no_comma: bool,
    // Accept the string "M,", interpreted as an empty message
    pub accept_m_empty_trailing_comma: bool,
}

impl MessageParseQuirks {
    pub const PERMISSIVE: Self = Self {
        accept_extra_elements: true,
        accept_trailing_whitespace: true,
        accept_s_nonzero_reconnect_arg: true,
        accept_s_negative_call_sign: true,
        accept_m_empty_no_comma: true,
        accept_m_empty_trailing_comma: true,
    };

    pub const PEDANTIC: Self = Self {
        accept_extra_elements: false,
        accept_trailing_whitespace: false,
        accept_s_nonzero_reconnect_arg: false,
        accept_s_negative_call_sign: false,
        accept_m_empty_no_comma: false,
        accept_m_empty_trailing_comma: false,
    };

    pub const DIXONARY_LIKE: Self = Self {
        accept_extra_elements: false,
        accept_trailing_whitespace: true,
        accept_s_nonzero_reconnect_arg: false,
        accept_s_negative_call_sign: false,
        accept_m_empty_no_comma: false,
        accept_m_empty_trailing_comma: true,
    };
}

impl Message {
    pub fn from_str_with_quirks(s: &str, q: &MessageParseQuirks) -> Result<Self, MessageParseErrorKind> {
        // We chunk the message string as a comma delimited list of strings.

        // Specified message strings normatively follow the pattern /^[A-Z](,-?[0-9]+)*$/.

        // When the "accept_trailing_whitespace" quirk is allowed, the pattern may also include trailing ASCII whitespace.

        // The first list element is matched by exact string comparison.

        // Subsequent (integer) elements are parsed as follows:
        // - Each subsequent expected list element is parsed as a native signed machine integer (IntT).
        // - Messages are rejected if an expected element fails to parse.
        // - A message is normally rejected if it contains more than an expected number of list elements.
        //     - When the "accept_extra_elements" quirk is allowed, a message is accepted if it contains more than an
        //       expected number of list elements, and is otherwise valid. The contents of the list past the last
        //       expected element are effectively unparsed and ignored.
        // - If the message is M type, the "accept_m_empty_trailing_comma" quirk enables matching of the special pattern
        //   '"M", followed by a comma, followed by nothing', which is interpreted as an empty message.

        // pedantic: Reject trailing whitespace
        let s_trim_end = s.trim_ascii_end();
        if !q.accept_trailing_whitespace && s_trim_end.len() != s.len() {
            return Err(MessageParseErrorKind::TrailingWhitespace);
        }

        let mut it = s_trim_end.split(',');
        match it.next() {
            Some("S") => {
                // S,<callsign> ; S,<callsign>,0
                // Callsign is required
                let call_sign = it
                    .next()
                    .ok_or(MessageParseErrorKind::TooFewSymbols)?
                    .parse::<IntT>()
                    .map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                // pedantic: Reject negative callsigns
                if !q.accept_s_negative_call_sign && call_sign < 0 {
                    return Err(MessageParseErrorKind::UnexpectedSymbolValue);
                }

                // Reconnect hint is optional but if it is present, it must be 0
                let reconnect = match it.next() {
                    Some(hint) => match hint.parse::<IntT>() {
                        Ok(0) => true,
                        // pedantic: Reject nonzero reconnect argument
                        _ if q.accept_s_nonzero_reconnect_arg => false,
                        _ => return Err(MessageParseErrorKind::UnexpectedSymbolValue),
                    },
                    None => false
                };

                // pedantic: Reject if some tokens are unconsumed
                if !q.accept_extra_elements && it.next().is_some() {
                    return Err(MessageParseErrorKind::TooManySymbols);
                };

                Ok(Self::SetCallSign { call_sign, reconnect })
            },
            Some("M") => {
                // M,<symbol>[,<symbol>...]
                let second = it.next();
                let message = if let Some(second) = second {
                    if second.is_empty() {
                        // M,
                        if q.accept_m_empty_trailing_comma && it.next().is_none() {
                            Vec::new()
                        } else {
                            Err(MessageParseErrorKind::UnexpectedSymbolValue)?
                        }
                    } else {
                        std::iter::once(second)
                            .chain(it)
                            .map(|x| x.parse::<IntT>())
                            .collect::<Result<Vec<_>, _>>()
                            .map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?
                    }
                } else {
                    // M
                    if q.accept_m_empty_no_comma {
                        Vec::new()
                    } else {
                        Err(MessageParseErrorKind::TooFewSymbols)?
                    }
                };

                Ok(Self::BroadcastMessage(message))
            },

            Some("R") => {
                // R,<callsign>,<sequence>,<symbol>[,<symbol>...]
                // Callsign is required
                let call_sign = it
                    .next().ok_or(MessageParseErrorKind::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                // Sequence is required
                let sequence_num = it
                    .next().ok_or(MessageParseErrorKind::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                let message: Vec<IntT> = it
                    .map(|x| x.parse::<IntT>()).collect::<Result<Vec<_>, _>>()
                    .map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                // TODO empty message quirks are only implemented for client->server direction.
                // server->client tightening/relaxation controls should be separately defined.

                // pedantic: Reject an empty message
                // if message.is_empty() {
                //     return Err(MessageParseErrorKind::TooFewSymbols);
                // }

                Ok(Self::ReceivedMessage { call_sign, sequence_num, message })
            },
            Some("K") => {
                // K,<callsign>
                // Callsign is required
                let call_sign = it
                    .next().ok_or(MessageParseErrorKind::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                // pedantic: Reject if some tokens are unconsumed
                if !q.accept_extra_elements && it.next().is_some() {
                    return Err(MessageParseErrorKind::TooManySymbols);
                };

                Ok(Self::CallSignOk(call_sign))
            },
            Some("E") => {
                // pedantic: Reject if some tokens are unconsumed
                if !q.accept_extra_elements && it.next().is_some() {
                    return Err(MessageParseErrorKind::TooManySymbols);
                };

                Ok(Self::ReconnectOk)
            },
            Some("U") => {
                // U,<callsign>
                // Callsign is required
                let call_sign = it
                    .next().ok_or(MessageParseErrorKind::TooFewSymbols)?
                    .parse::<IntT>().map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                // pedantic: Reject if some tokens are unconsumed
                if !q.accept_extra_elements && it.next().is_some() {
                    return Err(MessageParseErrorKind::TooManySymbols);
                };

                Ok(Self::CallSignInUse(call_sign))
            },
            Some("C") => {
                // C[,<callsign>...]
                let call_signs: Vec<IntT> = it
                    .map(|x| x.parse::<IntT>()).collect::<Result<Vec<_>, _>>()
                    .map_err(|_| MessageParseErrorKind::UnexpectedSymbolValue)?;

                Ok(Self::CallSignsUpdate(call_signs))
            },

            // Empty message string or unknown signifier
            Some(..) => {
                Err(MessageParseErrorKind::UnrecognizedSignifier)
            },

            // split should produce 1 token with an empty string
            None => {
                unreachable!()
            }
        }
    }
}

impl str::FromStr for Message {
    type Err = MessageParseErrorKind;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::from_str_with_quirks(s, &MessageParseQuirks::PERMISSIVE)
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
                if message.is_empty() {
                    // matches what Dixonary's server accepts as a valid empty message
                    // note that empty messages are out-of-spec and seen as pathological
                    write!(f, "M,")?;
                } else {
                    write!(f, "M")?;
                    for symbol in message {
                        write!(f, ",{}", symbol)?;
                    }
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
