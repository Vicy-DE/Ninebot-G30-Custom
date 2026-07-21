; VESC Lisp — Ninebot G30 Max Dashboard Integration
;
; Bridges the G30 dashboard to the VESC motor controller via Ninebot protocol.
; The dashboard handles throttle/brake ADC and sends values via protocol frame
; 0x65. This script reads them and controls the motor via app-adc-override.
;
; Protocol: Ninebot UART (0x5AA5 header, 115200 baud, half-duplex)
; Frame 0x65: dashboard -> VESC (throttle byte 5, brake byte 6)
; Frame 0x64: VESC -> dashboard (mode, battery, light, beep, speed, error)
;
; Reference: CRZX1337/g30-vesc-dash, m365fw/vesc_m365_dash

; ---------------------------------------------------------------------------
; User parameters
; ---------------------------------------------------------------------------

(def min-adc-throttle 0.1)
(def min-adc-brake 0.1)
(def min-speed 1)                         ; km/h — below this is "idle"
(def button-safety-speed (/ 0.1 3.6))     ; disable button above this speed (m/s)
(def show-batt-in-idle 1)                 ; show battery % instead of speed when idle

; Speed modes: (km/h, watts, current scale, field weakening)
(def eco-speed (/ 7 3.6))
(def eco-watts 400)
(def eco-current 0.6)
(def eco-fw 0)

(def drive-speed (/ 20 3.6))
(def drive-watts 500)
(def drive-current 0.7)
(def drive-fw 0)

(def sport-speed (/ 30 3.6))
(def sport-watts 700)
(def sport-current 1.0)
(def sport-fw 0)

; ---------------------------------------------------------------------------
; Protocol setup
; ---------------------------------------------------------------------------

(uart-start 115200 'half-duplex)
(gpio-configure 'pin-rx 'pin-mode-in-pu)
(app-adc-detach 3 1) ; use software ADC override

; Power-latch keep-alive: HIGH tells the Power-Latch Controller to keep the Daly
; discharge FET on; LOW (set on long-press OFF) makes the PLC send Daly 0xD9 OFF,
; cutting VESC power. Use any free lisp GPIO. See docs/POWER_LATCH_SCHEMATIC.md.
(def pin-keepalive 'pin-adc2)
(gpio-configure pin-keepalive 'pin-mode-out)
(gpio-write pin-keepalive 1)   ; assert keep-alive at boot

; TX frame buffer (15 bytes for display update)
(define tx-frame (array-create 15))
(bufset-u16 tx-frame 0 0x5AA5)  ; Ninebot header
(bufset-u8  tx-frame 2 0x06)    ; Payload length: 6 bytes
(bufset-u16 tx-frame 3 0x2021)  ; Source 0x20 (ESC), Dest 0x21 (BLE)
(bufset-u16 tx-frame 5 0x6400)  ; Frame code 0x64, sub 0x00

; RX buffer
(def uart-buf (array-create 64))

; ---------------------------------------------------------------------------
; State variables
; ---------------------------------------------------------------------------

(def off 0)
(def lock 0)
(def light 0)
(def speedmode 4)    ; 1=drive, 2=eco, 4=sport
(def feedback 0)     ; beep counter
(def presstime (systime))
(def presses 0)

; ---------------------------------------------------------------------------
; Throttle/brake input (frame 0x65)
; ---------------------------------------------------------------------------

(defun adc-input (buffer)
    (let ((throttle (/ (bufget-u8 uart-buf 5) 77.2))  ; 255/3.3 = 77.2
          (brake    (/ (bufget-u8 uart-buf 6) 77.2)))
        {
            (if (< throttle 0) (setf throttle 0))
            (if (> throttle 3.3) (setf throttle 3.3))
            (if (< brake 0) (setf brake 0))
            (if (> brake 3.3) (setf brake 3.3))
            (app-adc-override 0 throttle)
            (app-adc-override 1 brake)
        }
    )
)

; ---------------------------------------------------------------------------
; Backlight / headlight on the VESC PPM/servo output (GPIOB5)
; ---------------------------------------------------------------------------
; The light is switched by a MOSFET driver module fed from the servo/PPM pin.
; Enable "Servo Output" in VESC Tool (App Settings -> General) and set the PPM
; app Control Type = Off. set-servo 1.0 = light on, 0.0 = off.
; See docs/WIRING_PLAN_DALY_VESC.md §3.6 / §7.
;
; Alternative (clean GPIO level instead of a servo pulse) — uncomment if your
; MOSFET module needs a static high/low rather than a servo PWM pulse:
;   (gpio-configure 'pin-ppm 'pin-mode-out)
;   (defun update-light () (gpio-write 'pin-ppm (if (= off 1) 0 light)))

(defun update-light ()
    (set-servo (if (and (= off 0) (= light 1)) 1.0 0.0))
)

; ---------------------------------------------------------------------------
; Output control (lock, off)
; ---------------------------------------------------------------------------

(defun handle-features ()
    {
        (update-light)
        (if (or (= off 1) (= lock 1) (< (* (get-speed) 3.6) min-speed))
            (if (not (app-is-output-disabled))
                {
                    (app-adc-override 0 0)
                    (app-adc-override 1 0)
                    (app-disable-output -1)
                    (set-current 0)
                }
            )
            (if (app-is-output-disabled)
                (app-disable-output 0)
            )
        )
        (if (= lock 1)
            {
                (set-current-rel 0)
                (if (> (* (get-speed) 3.6) min-speed)
                    (set-brake-rel 1)
                    (set-brake-rel 0)
                )
            }
        )
    }
)

; ---------------------------------------------------------------------------
; Dashboard display update (frame 0x64)
; ---------------------------------------------------------------------------

(defun update-dash (buffer)
    {
        (var current-speed (* (get-speed) 3.6))
        (var battery (* (get-batt) 100))

        ; Mode field
        (if (= off 1)
            (bufset-u8 tx-frame 7 16)      ; off icon
            (if (= lock 1)
                (bufset-u8 tx-frame 7 32)   ; lock icon
                (if (or (> (get-temp-fet) 60) (> (get-temp-mot) 60))
                    (bufset-u8 tx-frame 7 (+ 128 speedmode))  ; temp warning
                    (bufset-u8 tx-frame 7 speedmode)
                )
            )
        )

        ; Battery field
        (bufset-u8 tx-frame 8 battery)

        ; Light field
        (if (= off 0)
            (bufset-u8 tx-frame 9 light)
            (bufset-u8 tx-frame 9 0)
        )

        ; Beep field
        (if (= lock 1)
            (if (> current-speed min-speed)
                (bufset-u8 tx-frame 10 1)
                (bufset-u8 tx-frame 10 0)
            )
            (if (> feedback 0)
                {
                    (bufset-u8 tx-frame 10 1)
                    (set 'feedback (- feedback 1))
                }
                (bufset-u8 tx-frame 10 0)
            )
        )

        ; Speed field (show battery % when idle if enabled)
        (if (and (= show-batt-in-idle 1) (<= current-speed min-speed))
            (bufset-u8 tx-frame 11 battery)
            (bufset-u8 tx-frame 11 current-speed)
        )

        ; Error field
        (bufset-u8 tx-frame 12 (get-fault))

        ; CRC: XOR 0xFFFF of byte sum from offset 2 to 12
        (var crc 0)
        (looprange i 2 13
            (set 'crc (+ crc (bufget-u8 tx-frame i)))
        )
        (set 'crc (bitwise-xor crc 0xFFFF))
        (bufset-u8 tx-frame 13 crc)
        (bufset-u8 tx-frame 14 (shr crc 8))

        (uart-write tx-frame)
    }
)

; ---------------------------------------------------------------------------
; Frame parser
; ---------------------------------------------------------------------------

(defun read-frames ()
    (loopwhile t
        {
            (uart-read-bytes uart-buf 3 0)
            (if (= (bufget-u16 uart-buf 0) 0x5AA5)
                {
                    (var len (bufget-u8 uart-buf 2))
                    (var crc len)
                    (if (and (> len 0) (< len 60))
                        {
                            (uart-read-bytes uart-buf (+ len 6) 0)
                            (let ((code (bufget-u8 uart-buf 2))
                                  (checksum (bufget-u16 uart-buf (+ len 4))))
                                {
                                    (looprange i 0 (+ len 4)
                                        (set 'crc (+ crc (bufget-u8 uart-buf i)))
                                    )
                                    (if (= checksum
                                            (bitwise-and
                                                (+ (shr (bitwise-xor crc 0xFFFF) 8)
                                                   (shl (bitwise-xor crc 0xFFFF) 8))
                                                65535))
                                        (handle-frame code)
                                    )
                                }
                            )
                        }
                    )
                }
            )
        }
    )
)

(defun handle-frame (code)
    {
        (if (= code 0x65)
            (adc-input uart-buf)
        )
        (if (= code 0x64)
            (update-dash uart-buf)
        )
    }
)

; ---------------------------------------------------------------------------
; Speed mode control
; ---------------------------------------------------------------------------

(defun configure-speed (speed watts current fw)
    {
        (conf-set 'max-speed speed)
        (conf-set 'l-watt-max watts)
        (conf-set 'l-current-max-scale current)
        (conf-set 'foc-fw-current-max fw)
    }
)

(defun apply-mode ()
    (if (= speedmode 1)
        (configure-speed drive-speed drive-watts drive-current drive-fw)
        (if (= speedmode 2)
            (configure-speed eco-speed eco-watts eco-current eco-fw)
            (if (= speedmode 4)
                (configure-speed sport-speed sport-watts sport-current sport-fw)
            )
        )
    )
)

; ---------------------------------------------------------------------------
; Button handling
; ---------------------------------------------------------------------------

(defun handle-button ()
    (if (= presses 1)
        ; Single press
        (if (= off 1)
            {
                (set 'off 0)
                (set 'feedback 1)
                (gpio-write pin-keepalive 1)   ; re-assert keep-alive (stay powered)
                (apply-mode)
            }
            (set 'light (bitwise-xor light 1))
        )
        ; Double press or more
        (if (>= presses 2)
            {
                (if (> (get-adc-decoded 1) min-adc-brake)
                    {
                        (set 'lock (bitwise-xor lock 1))
                        (set 'feedback 1)
                    }
                    {
                        (if (= lock 0)
                            {
                                (cond
                                    ((= speedmode 1) (set 'speedmode 4))
                                    ((= speedmode 2) (set 'speedmode 1))
                                    ((= speedmode 4) (set 'speedmode 2))
                                )
                                (apply-mode)
                            }
                        )
                    }
                )
            }
        )
    )
)

(defun handle-holding-button ()
    {
        (if (= (+ lock off) 0)
            {
                (set 'off 1)
                (set 'light 0)
                (set 'feedback 1)
                (update-light)                     ; turn the light off now
                (app-disable-output -1)            ; stop the motor before power cut
                (set-current 0)
                (gpio-write pin-keepalive 0)       ; request power-off: PLC sends Daly 0xD9 OFF → cut
                (apply-mode)
            }
        )
    }
)

(defun reset-button ()
    {
        (set 'presstime (systime))
        (set 'presses 0)
    }
)

(defun button-apply (button)
    {
        (var time-passed (- (systime) presstime))
        (var is-active (or (= off 1) (<= (get-speed) button-safety-speed)))

        (if (> time-passed 2500)
            (if (= button 0)
                (if (> time-passed 6000)
                    {
                        (if is-active (handle-holding-button))
                        (reset-button)
                    }
                )
                (if (> presses 0)
                    {
                        (if is-active (handle-button))
                        (reset-button)
                    }
                )
            )
        )
    }
)

(defun button-logic ()
    {
        (var buttonold 0)
        (loopwhile t
            {
                (var button (gpio-read 'pin-rx))
                (sleep 0.05)
                (var buttonconfirm (gpio-read 'pin-rx))
                (if (not (= button buttonconfirm))
                    (set 'button 0)
                )
                (if (> buttonold button)
                    {
                        (set 'presses (+ presses 1))
                        (set 'presstime (systime))
                    }
                    (button-apply button)
                )
                (set 'buttonold button)
                (handle-features)
            }
        )
    }
)

; ---------------------------------------------------------------------------
; Main
; ---------------------------------------------------------------------------

(apply-mode)
(spawn 150 read-frames)  ; UART reader thread
(button-logic)            ; Button handler in main thread (blocks)
