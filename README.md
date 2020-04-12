# Feather.M0.SimpleWebServerWiFi.servo
Arduino code for robot with arm controlled using WiFi server and HTML page
Robot based on Pololu Romi chassis with Pololu arm attached.  Three servos for arm and two motors to move chassis are controlled through WiFi server supported from Adafruit Feather M0 WiFi board.  Pololu motherboard provides power and control for motors, including shaft encoders.  WiFi server connects to local network and serves web page with slider controls to run arm servos and motors.
Arduino project requires Adafruit SAMD library and ocrdu/Arduino_SAMD21_turbo_PWM library to run motor PWM.
