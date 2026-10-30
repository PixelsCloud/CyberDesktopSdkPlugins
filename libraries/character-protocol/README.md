# Character protocol

This MIT library shares the character plugin's JSON/value contract with the host:
emotions, identifiers, bounded document/checkpoint parsing, story payload validation
and resource metadata validation. It uses the existing nlohmann/json parser; it
performs no network, file, credential or database operations.

Link `CyberDesktop::CharacterProtocol` and include `<CharacterChat/Protocol.h>`.
`cd_character_protocol_tests` checks the public boundary without Qt or the private
character service. Conversation state, rules, providers, persistence and supervised
workers remain in the private `character-chat` library and host.
