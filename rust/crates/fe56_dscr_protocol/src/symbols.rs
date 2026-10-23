enum EncryptedMessageKind {
    // A client "should not" issue a message beginning with AddKey/RemoveKey,
    // and "should" use them to control the display of messages beginning with EncryptedMessage.
    // The server does not treat these symbols specially.
    AddKey = -65534,
    RemoveKey = -65533,
    EncryptedMessage = -65535,
}

enum PlotMessageKind {
    // Orb plots, as seen in the game
    UnaryMinus = -1,
    Begin = -14,
    End = -15,
    Point = -52,
    Plot = -53,
}
