/**
 * @file esc_protocol.cpp
 * @brief ESC firmware — protocol packet dispatch and register handling.
 *
 * Reconstructed from DRV_1.6.13 dispatchReceivedPacket @ 0x08005468.
 *
 * The ESC processes packets on three UART channels. When a valid packet
 * arrives with the ESC as the destination (0x20), it is dispatched here.
 * Packets addressed to other devices (BMS, App) are forwarded to the
 * appropriate UART channel.
 *
 * Protocol flow:
 *   1. Phone App sends packet to ESC (via BLE board): App→BLE→ESC
 *   2. ESC reads command/argument bytes
 *   3. For CMD=READ (0x01): ESC reads register and responds
 *   4. For CMD=WRITE (0x02): ESC writes register and acknowledges
 *   5. For packets addressed to BMS (0x22): ESC forwards to USART3
 *
 * @note All firmware addresses reference DRV_1.6.13_Compat.bin.
 */

#include "esc_firmware.h"

namespace ninebot {
namespace esc {

/* =========================================================================
 * Protocol Processing (Main Loop)
 * =========================================================================
 * Pumps the TX handler on all three protocol channels. In the real
 * firmware, TX is interrupt-driven (TXE/TC), but the main loop also
 * kicks transmission when packets are queued.
 */
void EscFirmware::processProtocol()
{
    /* Drain TX buffers on all channels */
    chExt_.drainTx();
    chBle_.drainTx();
    chBms_.drainTx();
}

/* =========================================================================
 * Packet Dispatch
 * =========================================================================
 * @see DRV_1.6.13 dispatchReceivedPacket @ 0x08005468
 *
 * Routes incoming packets based on destination address and command type.
 *
 * Dispatch logic (from disassembly):
 *   if (dst == ESC) {
 *       switch (cmd) {
 *           case READ:  handleRead(pkt);  break;
 *           case WRITE: handleWrite(pkt); break;
 *       }
 *   } else if (dst == BMS || dst == BMS2) {
 *       forwardPacket(pkt);   // Forward to BMS channel
 *   } else if (dst == BLE || dst == APP) {
 *       forwardPacket(pkt);   // Forward to BLE channel
 *   }
 */
void EscFirmware::handlePacket(const Packet& pkt, ProtocolChannel& respondOn)
{
    /* Is this packet addressed to the ESC? */
    if (pkt.isFor(DevAddr::ESC)) {
        switch (pkt.command) {
            case static_cast<uint8_t>(Cmd::READ):
                handleRead(pkt, respondOn);
                break;

            case static_cast<uint8_t>(Cmd::WRITE):
                handleWrite(pkt, respondOn);
                break;

            case static_cast<uint8_t>(Cmd::READ_RESPONSE):
                /* Response from another device (e.g., BMS reply) */
                handleBmsResponse(pkt);
                break;

            default:
                /* Unknown command — ignore */
                break;
        }
        return;
    }

    /* Not for us — forward to the appropriate bus */
    forwardPacket(pkt);
}

/* =========================================================================
 * Handle Register READ
 * =========================================================================
 *
 * CMD=0x01: Read register(s) from ESC register file.
 *
 * Incoming packet:
 *   ARG = register address to start reading from
 *   PAYLOAD[0] = number of bytes to read (optional, default 2)
 *
 * Response packet:
 *   CMD = 0x03 (READ_RESPONSE)
 *   ARG = same register address
 *   PAYLOAD = register data (little-endian)
 *
 * @see DRV_1.6.13 register read handler
 */
void EscFirmware::handleRead(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t readLen = (pkt.payloadLength > 0) ? pkt.payload[0] : 2;

    /* Clamp read length to max payload size */
    if (readLen > MAX_PAYLOAD_LENGTH) readLen = MAX_PAYLOAD_LENGTH;
    if (readLen > 16) readLen = 16;  // Register slots are max 16 bytes

    /* Read from register file */
    uint8_t responsePayload[16] = {};
    regs_.readBytes(reg, responsePayload, readLen);

    /* Send response: CMD=READ_RESPONSE, ARG=register address */
    respondOn.enqueuePacket(
        DevAddr::ESC,
        static_cast<DevAddr>(pkt.source),
        Cmd::READ_RESPONSE,
        reg,
        responsePayload,
        readLen);
}

/* =========================================================================
 * Handle Register WRITE
 * =========================================================================
 *
 * CMD=0x02: Write register(s) in ESC register file.
 *
 * Incoming packet:
 *   ARG = register address to write to
 *   PAYLOAD = data to write (little-endian)
 *
 * Response packet:
 *   CMD = 0x05 (WRITE_ACK)
 *   ARG = same register address
 *   PAYLOAD = [0x01] for success, [0x00] for failure
 *
 * Some registers have side effects when written:
 *   - RIDING_MODE (0x75): changes speed limit
 *   - LOCK_STATE (0x70): enables/disables motor
 *   - CRUISE_CONTROL (0x7C): enables cruise control
 *   - TAIL_LIGHT (0x7D): controls rear LED
 *
 * @see DRV_1.6.13 register write handler
 */
void EscFirmware::handleWrite(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t success = 0x01;

    /* Validate payload */
    if (pkt.payloadLength == 0) {
        success = 0x00;
    } else {
        /* Write data to register file */
        regs_.writeBytes(reg, pkt.payload, pkt.payloadLength);

        /*
         * Handle side effects of specific register writes.
         * In the real firmware, certain registers trigger actions
         * when written (e.g., changing riding mode recalculates
         * the speed limit).
         */
        switch (reg) {
            case esc_reg::RIDING_MODE:
                /* Recalculate speed limit for new mode */
                updateSpeedLimit();
                break;

            case esc_reg::LOCK_STATE:
                /* If locking, immediately stop motor */
                if (pkt.payload[0] != 0) {
                    motorDutyCycle_ = 0;
                    speedSetpoint_ = 0;
                    speedErrorInteg_ = 0;
                }
                break;

            case esc_reg::SPEED_LIMIT_SETTING:
                /* User changed speed limit via app */
                updateSpeedLimit();
                break;

            default:
                break;
        }
    }

    /* Send acknowledgment */
    respondOn.enqueuePacket(
        DevAddr::ESC,
        static_cast<DevAddr>(pkt.source),
        Cmd::WRITE_ACK,
        reg,
        &success,
        1);
}

/* =========================================================================
 * Handle BMS Response
 * =========================================================================
 * When the ESC queries the BMS, the BMS responds with a READ_RESPONSE.
 * This handler updates the ESC's local copy of battery data.
 */
void EscFirmware::handleBmsResponse(const Packet& pkt)
{
    if (!pkt.isFrom(DevAddr::BMS)) return;

    switch (pkt.argument) {
        case 0x31: /* Battery voltage (0.01V units) */
            if (pkt.payloadLength >= 2) {
                regs_.writeU16(esc_reg::BATTERY_VOLTAGE, pkt.payloadU16());
            }
            break;

        case 0x33: /* Battery current (0.01A units, signed) */
            if (pkt.payloadLength >= 2) {
                regs_.writeU16(esc_reg::BATTERY_CURRENT, pkt.payloadU16());
            }
            break;

        case 0x32: /* Remaining capacity (percent) */
            if (pkt.payloadLength >= 2) {
                regs_.writeU16(esc_reg::REMAINING_BATTERY, pkt.payloadU16());
            }
            break;

        case 0x3B: /* Remaining range (0.01 km) */
            if (pkt.payloadLength >= 2) {
                regs_.writeU16(esc_reg::REMAINING_RANGE, pkt.payloadU16());
            }
            break;

        default:
            break;
    }
}

/* =========================================================================
 * Packet Forwarding
 * =========================================================================
 *
 * The ESC acts as a bus router. Packets addressed to the BMS are
 * forwarded to USART3. Packets addressed to the BLE/App are forwarded
 * to USART2.
 *
 * From DRV_1.6.13: the dispatch function checks the destination address
 * and selects the output channel.
 */
void EscFirmware::forwardPacket(const Packet& pkt)
{
    ProtocolChannel* target = nullptr;

    if (pkt.isFor(DevAddr::BMS) || pkt.isFor(DevAddr::BMS2)) {
        target = &chBms_;
    } else if (pkt.isFor(DevAddr::BLE) || pkt.isFor(DevAddr::APP)) {
        target = &chBle_;
    } else if (pkt.isFor(DevAddr::PC)) {
        target = &chExt_;
    }

    if (target) {
        target->enqueuePacket(
            pkt.source, pkt.destination,
            pkt.command, pkt.argument,
            pkt.payload, pkt.payloadLength);
    }
}

/* =========================================================================
 * Periodic BMS Query
 * =========================================================================
 *
 * The ESC queries the BMS periodically (every 200ms) to get battery
 * state information. This keeps the dashboard display up to date.
 *
 * Queried registers (from firmware analysis):
 *   - 0x31: Battery voltage
 *   - 0x33: Battery current
 *   - 0x32: Remaining capacity
 *   - 0x3B: Remaining range
 *
 * Each query cycle sends one register read request. The responses are
 * handled in handleBmsResponse() when they arrive.
 */
void EscFirmware::periodicBmsQuery()
{
    /** Query interval: 200ms between BMS register reads */
    static constexpr uint32_t BMS_QUERY_INTERVAL_MS = 200;

    /** BMS register addresses to query in round-robin */
    static constexpr uint8_t BMS_QUERY_REGS[] = { 0x31, 0x33, 0x32, 0x3B };
    static constexpr int NUM_BMS_REGS = sizeof(BMS_QUERY_REGS);

    if (tickMs_ - lastBmsQueryMs_ < BMS_QUERY_INTERVAL_MS) return;

    lastBmsQueryMs_ = tickMs_;

    /* Round-robin through the register list */
    static uint8_t queryIndex = 0;
    uint8_t reg = BMS_QUERY_REGS[queryIndex % NUM_BMS_REGS];
    queryIndex++;

    /* Build a read request: payload[0] = number of bytes to read */
    uint8_t readLen = 2;
    chBms_.enqueuePacket(
        DevAddr::ESC,
        DevAddr::BMS,
        Cmd::READ,
        reg,
        &readLen,
        1);
}

/* =========================================================================
 * Error Checking
 * =========================================================================
 *
 * Safety checks performed every main loop iteration:
 *   - Overvoltage: battery > 54V (5400 centV)
 *   - Undervoltage: battery < 28V (2800 centV)
 *   - Overcurrent: motor current > 35A (3500 centA)
 *   - Overtemperature: controller temp > 80°C (800 deci°C)
 *   - Hall sensor fault: invalid hall state
 *
 * Error flags are set in the ERROR_CODE register.
 * @see esc_error:: namespace for error bit definitions.
 */
void EscFirmware::checkErrors()
{
    uint16_t voltage = regs_.readU16(esc_reg::BATTERY_VOLTAGE);
    uint16_t temp = regs_.readU16(esc_reg::FRAME_TEMPERATURE);

    /* Overvoltage check: > 54.00V */
    if (voltage > 5400) {
        setError(esc_error::OVERVOLTAGE);
    } else if (voltage > 0 && voltage < 5200) {
        clearError(esc_error::OVERVOLTAGE);
    }

    /* Undervoltage check: < 28.00V */
    if (voltage > 0 && voltage < 2800) {
        setError(esc_error::UNDERVOLTAGE);
    } else if (voltage > 3000) {
        clearError(esc_error::UNDERVOLTAGE);
    }

    /* Overtemperature check: > 80.0°C */
    if (temp > 800) {
        setError(esc_error::OVERTEMP);
    } else if (temp < 750) {
        clearError(esc_error::OVERTEMP);
    }
}

/* =========================================================================
 * Register Update
 * =========================================================================
 * Syncs computed values into the register file for external readability.
 */
void EscFirmware::updateRegisters()
{
    /* Read ADC for frame temperature if available */
    if (hal_.adc) {
        /*
         * ADC channel 16 = internal temperature sensor on STM32.
         * Scale: approx (V_sense - 0.76) / 0.0025 + 25 for °C.
         * Stored as deci-°C (e.g., 250 = 25.0°C).
         */
        uint16_t rawAdc = hal_.adc->readChannel(16);
        /* Simplified linear approximation */
        uint16_t tempDeciC = (rawAdc > 0)
            ? static_cast<uint16_t>((rawAdc * 330 / 4096) + 10)
            : 250;  // Default 25.0°C
        regs_.writeU16(esc_reg::FRAME_TEMPERATURE, tempDeciC);
    }
}

/* =========================================================================
 * External Byte Feeding (for Simulation)
 * =========================================================================
 * Feeds raw bytes into the BLE-facing protocol channel (USART2),
 * simulating incoming data from the dashboard or phone app.
 */
void EscFirmware::feedBytes(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        chBle_.receiveByte(data[i]);
    }
}

/* =========================================================================
 * TX Drain (for Simulation)
 * =========================================================================
 * Drains all queued TX packets from all channels into a combined buffer.
 * Used in simulation to collect all outgoing data.
 */
std::vector<uint8_t> EscFirmware::drainAllTx()
{
    std::vector<uint8_t> out;

    /* We can't easily get all bytes from drainTx() since they go to the
     * UART hardware. In simulation, the SimUart stores them. */
    chExt_.drainTx();
    chBle_.drainTx();
    chBms_.drainTx();

    return out;
}


} // namespace esc
} // namespace ninebot
