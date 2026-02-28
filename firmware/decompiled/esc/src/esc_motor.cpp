/**
 * @file esc_motor.cpp
 * @brief ESC firmware — motor control, hall sensor decoding, and speed PID.
 *
 * Reconstructed from DRV_1.6.13 TIM1_UP_IRQHandler @ 0x08005E74 and
 * related motor control functions.
 *
 * Motor drive topology:
 *   The G30 Max uses a 3-phase BLDC motor with 15 pole pairs.
 *   The ESC drives it using 6-step commutation with 3 half-bridge FET pairs.
 *   TIM1 generates center-aligned PWM with complementary outputs and dead-time.
 *
 *   Hall sensor inputs (GPIOB pins):
 *     - Hall A: PB5  (TIM3_CH2 input capture, or polled GPIO)
 *     - Hall B: PB6  (TIM4_CH1 or polled)
 *     - Hall C: PB7  (TIM4_CH2 or polled)
 *
 *   PWM outputs on TIM1:
 *     - CH1/CH1N: Phase A high/low FET  (PA8 / PB13)
 *     - CH2/CH2N: Phase B high/low FET  (PA9 / PB14)
 *     - CH3/CH3N: Phase C high/low FET  (PA10 / PB15)
 *
 * Speed calculation:
 *   RPM = (hallEdgeCount / HALL_EDGES_PER_REV) * 60000 / deltaMs
 *   Speed (km/h) = RPM × WHEEL_CIRC_MM / 1,000,000 × 60
 *
 * @note All firmware addresses reference DRV_1.6.13_Compat.bin.
 */

#include "esc_firmware.h"

namespace ninebot {
namespace esc {

/* =========================================================================
 * Hall Sensor → Commutation Step Lookup Table
 * =========================================================================
 *
 * Maps 3-bit hall sensor state to the correct commutation step.
 * Hall state = (HallC << 2) | (HallB << 1) | HallA
 *
 * Valid states: 1-6 (states 0 and 7 are invalid / error conditions)
 *
 * Commutation table (motor direction: forward):
 *   Hall  Energized     PWM Phase   Low Phase   Float Phase
 *   001   Step 0        A (high)    B (low)     C (float)
 *   011   Step 1        A (high)    C (low)     B (float)
 *   010   Step 2        B (high)    C (low)     A (float)
 *   110   Step 3        B (high)    A (low)     C (float)
 *   100   Step 4        C (high)    A (low)     B (float)
 *   101   Step 5        C (high)    B (low)     A (float)
 *
 * In the firmware, this lookup table is stored in flash. The TIM1
 * compare and output enable registers are set based on the step.
 */

/**
 * Commutation state: which phases to energize for each step.
 *
 * Index 0 and 7 are invalid (set to 0xFF to trigger error detection).
 * Bits: [5:4]=Phase C, [3:2]=Phase B, [1:0]=Phase A
 *   00=float, 01=PWM (high-side), 10=low (low-side active)
 */
static constexpr uint8_t HALL_TO_STEP[8] = {
    0xFF,   // 0b000: invalid — no hall sensor active
    0,      // 0b001: Step 0 — A↑, B↓, C~
    2,      // 0b010: Step 2 — B↑, C↓, A~
    1,      // 0b011: Step 1 — A↑, C↓, B~
    4,      // 0b100: Step 4 — C↑, A↓, B~
    5,      // 0b101: Step 5 — C↑, B↓, A~
    3,      // 0b110: Step 3 — B↑, A↓, C~
    0xFF    // 0b111: invalid — all hall sensors active
};

/*
 * Phase configuration per commutation step.
 * Each row: [PWM_compare1, PWM_compare2, PWM_compare3]
 *   0     = channel off (floating)
 *   duty  = PWM modulated (high-side on)
 *   max+1 = channel forced low (low-side through)
 *
 * The actual PWM duty is substituted at runtime for the "duty" entries.
 */

/* =========================================================================
 * Motor Commutation ISR
 * =========================================================================
 *
 * @see DRV_1.6.13 TIM1_UP_IRQHandler @ 0x08005E74
 *
 * Called at 16kHz (every TIM1 period). Reads hall sensors and sets
 * the correct PWM outputs for 6-step commutation.
 *
 * Simplified reconstruction:
 *   1. Read hall pins from GPIOB
 *   2. Map to commutation step via lookup table
 *   3. Set TIM1 CCR1/2/3 for the active phase pair
 *   4. If hall state changed, increment edge counter for speed calc
 */
void EscFirmware::motorCommutationIsr()
{
    /* Read hall sensors from GPIOB pins 5, 6, 7 */
    uint8_t newHall = 0;
    if (hal_.gpioB) {
        uint16_t portB = hal_.gpioB->readPort();
        newHall = ((portB >> 5) & 0x07);  // Bits 5,6,7 → 3-bit hall state
    }

    /* Validate hall sensor state */
    uint8_t step = HALL_TO_STEP[newHall & 0x07];
    if (step == 0xFF) {
        /*
         * Invalid hall state (0x00 or 0x07). In the real firmware this
         * sets an error flag. Both states indicate sensor failure
         * or wiring fault.
         */
        if (hal_.timerMotor) {
            hal_.timerMotor->setCompare(1, 0);
            hal_.timerMotor->setCompare(2, 0);
            hal_.timerMotor->setCompare(3, 0);
        }
        return;
    }

    /* Detect hall state transition (for speed measurement) */
    if (newHall != hallState_) {
        hallEdgeCount_++;
        hallState_ = newHall;
    }

    /* Check if motor should be off (locked, error, or zero throttle) */
    if (isLocked() || errorCode() != 0 || motorDutyCycle_ == 0) {
        if (hal_.timerMotor) {
            hal_.timerMotor->setCompare(1, 0);
            hal_.timerMotor->setCompare(2, 0);
            hal_.timerMotor->setCompare(3, 0);
        }
        return;
    }

    /*
     * Apply commutation pattern.
     *
     * For each commutation step, one phase gets PWM (high-side),
     * one gets forced low (low-side through), and one floats.
     *
     * The duty cycle modulates the PWM phase. Setting compare=0
     * floats the output; compare=duty applies PWM; compare=PERIOD
     * would force the high-side on (not used in 6-step).
     *
     * The firmware uses the TIM1 complementary outputs feature:
     * setting CCR to duty makes CH active and CHN gives the dead-time
     * protected complementary signal. Setting CCR=0 disables both.
     */
    uint16_t duty = motorDutyCycle_;
    uint16_t ch1 = 0, ch2 = 0, ch3 = 0;

    switch (step) {
        case 0: ch1 = duty; ch2 = 1; ch3 = 0; break;  // A↑ B↓ C~
        case 1: ch1 = duty; ch2 = 0; ch3 = 1; break;  // A↑ C↓ B~
        case 2: ch1 = 0; ch2 = duty; ch3 = 1; break;  // B↑ C↓ A~
        case 3: ch1 = 1; ch2 = duty; ch3 = 0; break;  // B↑ A↓ C~
        case 4: ch1 = 1; ch2 = 0; ch3 = duty; break;  // C↑ A↓ B~
        case 5: ch1 = 0; ch2 = 1; ch3 = duty; break;  // C↑ B↓ A~
        default: break;
    }

    if (hal_.timerMotor) {
        hal_.timerMotor->setCompare(1, ch1);
        hal_.timerMotor->setCompare(2, ch2);
        hal_.timerMotor->setCompare(3, ch3);
    }
}

/* =========================================================================
 * Motor Control Update (Main Loop)
 * =========================================================================
 *
 * Called from main loop iteration. Calculates speed from hall edges,
 * runs the speed PID controller, and updates the motor duty cycle.
 *
 * Speed measurement window: 100ms (10 Hz update rate)
 *
 * PID controller (simplified from firmware analysis):
 *   - Kp = 8 (proportional gain)
 *   - Ki = 1 (integral gain, accumulated per 100ms)
 *   - No derivative term observed in the firmware
 *   - Output clamped to [0, MOTOR_PWM_PERIOD]
 */
void EscFirmware::updateMotorControl()
{
    /* Speed calculation: every 100ms */
    uint32_t elapsed = tickMs_ - lastSpeedCalcMs_;
    if (elapsed < 100) return;

    lastSpeedCalcMs_ = tickMs_;

    /*
     * Calculate electrical RPM from hall edge count.
     *
     * Formula:
     *   electricalRPM = (hallEdgeCount / HALL_EDGES_PER_REV) * (60000 / elapsed)
     *      = hallEdgeCount * 60000 / (HALL_EDGES_PER_REV * elapsed)
     *
     * Speed in 0.01 km/h:
     *   speed_centi = electricalRPM * WHEEL_CIRC_MM * 60 / 1_000_000
     *               = electricalRPM * WHEEL_CIRC_MM / 16667
     *
     * Combined:
     *   speed_centi = hallEdgeCount * 60000 * WHEEL_CIRC_MM
     *                 / (HALL_EDGES_PER_REV * elapsed * 16667)
     *
     * Simplified (with constant folding):
     *   speed_centi = hallEdgeCount * WHEEL_CIRC_MM * 3600
     *                 / (HALL_EDGES_PER_REV * elapsed)
     */
    uint32_t speedCenti = 0;
    if (elapsed > 0 && hallEdgeCount_ > 0) {
        speedCenti = (hallEdgeCount_ * WHEEL_CIRC_MM * 3600UL) /
                     (HALL_EDGES_PER_REV * elapsed);
    }

    /* Store current speed in register (units: 0.01 km/h in firmware) */
    regs_.writeU16(esc_reg::CURRENT_SPEED, static_cast<uint16_t>(
        speedCenti > 0xFFFF ? 0xFFFF : speedCenti));

    /* Update hall-based speed register */
    regs_.writeU16(esc_reg::MOTOR_HALL_SPEED, static_cast<uint16_t>(
        speedCenti > 0xFFFF ? 0xFFFF : speedCenti));

    /* Accumulate distance from hall edges */
    if (hallEdgeCount_ > 0) {
        uint32_t distMm = (hallEdgeCount_ * WHEEL_CIRC_MM) / HALL_EDGES_PER_REV;
        uint32_t tripDist = regs_.readU32(esc_reg::TRIP_DISTANCE);
        uint32_t totalDist = regs_.readU32(esc_reg::TOTAL_DISTANCE);
        regs_.writeU32(esc_reg::TRIP_DISTANCE, tripDist + distMm);
        regs_.writeU32(esc_reg::TOTAL_DISTANCE, totalDist + distMm);
    }

    /* Reset edge counter for next measurement window */
    hallEdgeCount_ = 0;

    /*
     * Speed PID Controller
     *
     * The firmware implements a PI controller to match the motor speed
     * to the throttle setpoint while respecting the speed limit.
     *
     * The speed limit is selected by the riding mode and can be
     * overridden via register write from the app.
     */
    uint16_t speedLimitCenti = regs_.readU16(esc_reg::SPEED_LIMIT_CURRENT);
    uint16_t targetSpeed = speedSetpoint_;

    /* Cap target to speed limit */
    if (targetSpeed > speedLimitCenti) {
        targetSpeed = speedLimitCenti;
    }

    /* If locked or error, force zero */
    if (isLocked() || errorCode() != 0) {
        targetSpeed = 0;
    }

    /* PI controller */
    int32_t error = static_cast<int32_t>(targetSpeed) -
                    static_cast<int32_t>(speedCenti);

    /* Proportional term (Kp = 8) */
    int32_t pTerm = error * 8;

    /* Integral term (Ki = 1, accumulated) */
    speedErrorInteg_ += error;

    /* Anti-windup: clamp integral to prevent overshoot */
    const int32_t MAX_INTEG = MOTOR_PWM_PERIOD * 4;
    if (speedErrorInteg_ > MAX_INTEG) speedErrorInteg_ = MAX_INTEG;
    if (speedErrorInteg_ < -MAX_INTEG) speedErrorInteg_ = -MAX_INTEG;

    int32_t output = pTerm + speedErrorInteg_;

    /* Clamp output to valid duty cycle range */
    if (output < 0) output = 0;
    if (output > MOTOR_PWM_PERIOD) output = MOTOR_PWM_PERIOD;

    motorDutyCycle_ = static_cast<uint16_t>(output);

    /* Apply current limit from riding mode */
    uint8_t mode = regs_.readU8(esc_reg::RIDING_MODE);
    if (mode < 3) {
        uint16_t currentLim = MODE_CURRENT_LIMIT[mode];
        /* Read measured current from ADC (stored in register) */
        int16_t measCurrent = static_cast<int16_t>(
            regs_.readU16(esc_reg::BATTERY_CURRENT));
        if (measCurrent > currentLim) {
            /* Back off duty cycle proportionally */
            motorDutyCycle_ = motorDutyCycle_ * currentLim / measCurrent;
        }
    }
}

/* =========================================================================
 * Speed Limit Update
 * =========================================================================
 * Applies the riding mode's speed limit and any user-configured limit.
 */
void EscFirmware::updateSpeedLimit()
{
    uint8_t mode = regs_.readU8(esc_reg::RIDING_MODE);
    if (mode >= 3) mode = 1;  // Default to D mode if invalid

    uint16_t modeLimit = MODE_SPEED_LIMIT[mode];
    uint16_t userLimitKmh = regs_.readU16(esc_reg::SPEED_LIMIT_SETTING);

    /* Apply user limit (km/h → 0.001 km/h units) if set */
    uint16_t effectiveLimit = modeLimit;
    if (userLimitKmh > 0) {
        uint32_t userLimit = static_cast<uint32_t>(userLimitKmh) * 1000;
        if (userLimit < modeLimit) {
            effectiveLimit = static_cast<uint16_t>(userLimit);
        }
    }

    regs_.writeU16(esc_reg::SPEED_LIMIT_CURRENT, effectiveLimit);
}

/* =========================================================================
 * Control Setters
 * ========================================================================= */

void EscFirmware::setSpeed(uint16_t speed_milli_kmh)
{
    speedSetpoint_ = speed_milli_kmh;
    regs_.writeU16(esc_reg::CURRENT_SPEED, speed_milli_kmh);
}

void EscFirmware::setRidingMode(RidingMode mode)
{
    regs_.writeU8(esc_reg::RIDING_MODE, static_cast<uint8_t>(mode));
    updateSpeedLimit();
}

void EscFirmware::setLocked(bool locked)
{
    regs_.writeU8(esc_reg::LOCK_STATE, locked ? 1 : 0);
}

void EscFirmware::setError(uint16_t errorBits)
{
    uint16_t current = regs_.readU16(esc_reg::ERROR_CODE);
    regs_.writeU16(esc_reg::ERROR_CODE, current | errorBits);
}

void EscFirmware::clearError(uint16_t errorBits)
{
    uint16_t current = regs_.readU16(esc_reg::ERROR_CODE);
    regs_.writeU16(esc_reg::ERROR_CODE, current & ~errorBits);
}

void EscFirmware::setBatteryVoltage(uint16_t voltage_centV)
{
    regs_.writeU16(esc_reg::BATTERY_VOLTAGE, voltage_centV);
}

void EscFirmware::setBatteryCurrent(int16_t current_centA)
{
    regs_.writeU16(esc_reg::BATTERY_CURRENT,
                   static_cast<uint16_t>(current_centA));
}


} // namespace esc
} // namespace ninebot
