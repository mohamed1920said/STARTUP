#include "LoraProtocol.h"
#include <cstring>

LoraProtocolManager::LoraProtocolManager(LoRadio& radio, CryptoEngine& crypto)
    : _radio(radio), _crypto(crypto) {
    for (auto& entry : _node_keys) {
        entry.active = false;
        entry.last_seq = 0;
    }
}

void LoraProtocolManager::begin(float frequency_mhz) {
    _radio.begin(frequency_mhz);
    _radio.onReceive([this](const uint8_t* buf, size_t len) {
        if (len <= LORA_MAX_PAYLOAD) {
            std::memcpy(this->_rx_buf, buf, len);
            this->_rx_len = len;
        }
    });
    _radio.setRxTimeout(0);
    _radio.startReceive();
}

void LoraProtocolManager::process() {
    if (_rx_len == 0) return;
    size_t len = _rx_len;
    _rx_len = 0;
    handleRxFrame(_rx_buf, len);
    _radio.startReceive();
}

void LoraProtocolManager::handleRxFrame(const uint8_t* buf, size_t len) {
    if (len < LORA_HEADER_SIZE + MIC_SIZE) {
        if (_failed_cb) _failed_cb(0, PacketType::HEARTBEAT, 0, "too_short");
        return;
    }
    LoraFrame frame;
    std::memcpy(frame.node_id, buf, NODE_ID_SIZE);
    std::memcpy(frame.iv, buf + NODE_ID_SIZE, IV_NONCE_SIZE);
    frame.pkt_type = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    const size_t hdr_size = LORA_HEADER_SIZE;
    const size_t ct_len = len - hdr_size - MIC_SIZE;
    frame.ciphertext_len = ct_len;
    if (ct_len > LORA_MAX_CIPHERTEXT) return;
    std::memcpy(frame.ciphertext, buf + hdr_size, ct_len);
    std::memcpy(frame.mic, buf + hdr_size + ct_len, MIC_SIZE);
    NodeId node_id = static_cast<NodeId>(frame.node_id[0] << 8 | frame.node_id[1]);
    uint32_t sequence = 0;
    for (int i = 0; i < 8; ++i) {
        sequence = (sequence << 8) | frame.iv[4 + i];
    }
    NodeKeyEntry* entry = findNode(node_id);
    if (!entry || !entry->active) {
        if (frame.pkt_type == static_cast<uint8_t>(PacketType::REGISTER_REQ)) {
            if (_decrypted_cb)
                _decrypted_cb(node_id, PacketType::REGISTER_REQ,
                              frame.ciphertext, ct_len, sequence);
        } else if (_failed_cb) {
            _failed_cb(node_id, static_cast<PacketType>(frame.pkt_type), sequence, "unknown");
        }
        return;
    }
    if (sequence <= entry->last_seq) {
        if (_failed_cb) _failed_cb(node_id, static_cast<PacketType>(frame.pkt_type), sequence, "replay");
        return;
    }
    CryptoEngine node_crypto;
    if (!node_crypto.begin(entry->key, AES128_KEY_SIZE)) return;
    uint8_t plaintext[LORA_MAX_CIPHERTEXT];
    std::memcpy(plaintext, frame.ciphertext, ct_len);
    if (!node_crypto.decrypt(plaintext, ct_len, frame.pkt_type, sequence, node_id, frame)) {
        if (_failed_cb) _failed_cb(node_id, static_cast<PacketType>(frame.pkt_type), sequence, "decrypt_fail");
        return;
    }
    entry->last_seq = sequence;
    if (_decrypted_cb)
        _decrypted_cb(node_id, static_cast<PacketType>(frame.pkt_type), plaintext, ct_len, sequence);
}

bool LoraProtocolManager::sendTo(NodeId target, PacketType type,
                                  const uint8_t* plaintext, size_t len,
                                  uint32_t sequence) {
    NodeKeyEntry* entry = findNode(target);
    if (!entry || !entry->active) return false;
    CryptoEngine node_crypto;
    if (!node_crypto.begin(entry->key, AES128_KEY_SIZE)) return false;
    uint8_t enc_buf[LORA_MAX_CIPHERTEXT];
    std::memcpy(enc_buf, plaintext, len);
    LoraFrame frame;
    frame.ciphertext_len = len;
    frame.node_id[0] = static_cast<uint8_t>(target >> 8);
    frame.node_id[1] = static_cast<uint8_t>(target & 0xFF);
    frame.pkt_type = static_cast<uint8_t>(type);
    if (!node_crypto.encrypt(enc_buf, len, static_cast<uint8_t>(type), sequence, target, frame))
        return false;
    uint8_t tx_buf[LORA_MAX_PAYLOAD];
    size_t offset = 0;
    std::memcpy(tx_buf + offset, frame.node_id, NODE_ID_SIZE); offset += NODE_ID_SIZE;
    std::memcpy(tx_buf + offset, frame.iv, IV_NONCE_SIZE); offset += IV_NONCE_SIZE;
    tx_buf[offset++] = frame.pkt_type;
    std::memcpy(tx_buf + offset, enc_buf, len); offset += len;
    std::memcpy(tx_buf + offset, frame.mic, MIC_SIZE); offset += MIC_SIZE;
    _radio.send(tx_buf, offset);
    return true;
}

bool LoraProtocolManager::sendRaw(NodeId target, PacketType type,
                                   const uint8_t* data, size_t data_len) {
    uint8_t tx_buf[LORA_MAX_PAYLOAD];
    size_t offset = 0;
    tx_buf[offset++] = static_cast<uint8_t>(target >> 8);
    tx_buf[offset++] = static_cast<uint8_t>(target & 0xFF);
    std::memset(tx_buf + offset, 0, IV_NONCE_SIZE); offset += IV_NONCE_SIZE;
    tx_buf[offset++] = static_cast<uint8_t>(type);
    std::memcpy(tx_buf + offset, data, data_len); offset += data_len;
    std::memset(tx_buf + offset, 0, MIC_SIZE); offset += MIC_SIZE;
    _radio.send(tx_buf, offset);
    return true;
}

void LoraProtocolManager::registerNodeKey(NodeId node_id, const uint8_t* key) {
    NodeKeyEntry* entry = findNode(node_id);
    if (!entry) {
        for (auto& e : _node_keys) {
            if (!e.active) { entry = &e; break; }
        }
    }
    if (!entry) return;
    entry->id = node_id;
    std::memcpy(entry->key, key, AES128_KEY_SIZE);
    entry->active = true;
    entry->last_seq = 0;
}

void LoraProtocolManager::removeNodeKey(NodeId node_id) {
    NodeKeyEntry* e = findNode(node_id);
    if (e) e->active = false;
}

auto LoraProtocolManager::findNode(NodeId id) -> NodeKeyEntry* {
    for (auto& e : _node_keys) {
        if (e.active && e.id == id) return &e;
    }
    return nullptr;
}
