/******************************************************************
Created with PROGRAMINO IDE for Arduino - 23.05.2023 | 10:21:23
Project     :
Libraries   :
Author      :  Chris74

Description :  e-bike pushbutton to PAS - Arduino Uno/Nano
- Simulation pedalage et envoi au controleur

Download this code on Github : 


Doc PAS ebikes.ca (Signal Types for Basic PAS Sensors) : 
- https://ebikes.ca/learn/pedal-assist.html
Doc PAS on pedelecforum.de
- https://www.pedelecforum.de/forum/index.php?threads/funktionsweise-digitaler-pas-sensoren-details-fuer-interessierte.9539/


Discussion Cyclurba (Soupaloignon) : 
- https://cyclurba.fr/forum/741850/arduino-l-assistance-d-un-vae.html?from=11&discussionID=31032&messageID=741850&rubriqueID=87
- https://cyclurba.fr/forum/738995/fonctionnent-capteurs-pn-dalage.html?discussionID=5512

Other source : Pedelecs forum 
- https://www.pedelecs.co.uk/forum/threads/how-to-add-a-throttle-to-a-carrera-vengeance-e-spec.31483/


******************************************************************/

// Install these 2 librairy with Arduino IDE librairy manager or manual (see url)

#include "Button2.h"        // Button2.h  https://github.com/LennartHennigs/Button2

#include <TimerOne.h>       // TimerOne.h https://github.com/PaulStoffregen/TimerOne   

// -- init objet button2
Button2 btn;

// -- default const for button2.h
/*
- DEBOUNCE_MS 50
- LONGCLICK_MS 200
- DOUBLECLICK_MS 300
// to set :
void setDebounceTime(unsigned int ms)
void setLongClickTime(unsigned int ms)
void setDoubleClickTime(unsigned int ms)
*/



// -------- GPIO --------------

const int BUTTON_PIN = 4; // Push button D3

const int PAS_PIN = 2;    // Interrupt(0) : PAS D2 
// without resistor -> INPUT_PULLUP 

const int BRAKE_PIN = 3;  // Interrupt(1) : Brake sensor (not mandatory but recommended if cruise control !!)  
// without resistor -> INPUT_PULLUP 

const int LED_PIN = 13;   // CONTROLER PAS and LED (D13 = LED_BUILTIN)


// Note : Avec input_pullup les signaux sont inverse ! 
// bouton appuye ou contacteur = LOW / Off / 0  


// -------- CONSTANTES PROG ------------------------

// -- Cruise-control true or false (default : false)
// -- activate by double-click, desactivate by click ou double-click
// -- Warning : Using a ebike brake cut-off is not mandatory but very recommended if cruise control !

#define USE_CRUISE true 

#define BRAKE_MODE 1  // if ebike brake cut-off is installed, mode 0= normaly low - mode 1= normaly high (default)
// - info : https://endless-sphere.com/sphere/threads/brake-high-brake-low.52391/


// -- reglages nb. d'aimants et simulation RPM pedalage
const int   nb_poles  = 6;      // nb. d'aimants (nb. of magnets) sur le disque du PAS
const int   simul_rpm = 40;     // RPM simulation pedalier

// -- reglage duty cycle signal High, cf doc ebikes.ca : https://ebikes.ca/learn/pedal-assist.html 
// -- attention var float : si nombre entier ajouter au moins une decimal .0 Par exemple 60% indiquer 60.0% !!
const float duty_cycle = 56.70;  // % duty high signal (56.70% on my PAS, mode varied width, but 57.0% normaly Ok)




// ******** Calcul constantes : ne pas modifier a partir d'ici ! ***********
// **************************************************************

// ne pas enlever les decimales aux chiffres entiers !
const int simul_pas = (1000.0 / (simul_rpm / 60.0 * nb_poles)) * (duty_cycle/100);   // Signal PAS high lent en ms, % duty_cycle

// calc ratio low (high/low), round 2 decimale
const float t1 = duty_cycle / (100 - duty_cycle);
const float ratio_low = floor(100*t1+0.5)/100; // round decimal to 2 digit (10 for 1 digit)

// -- expl. periode PWM ; 60Rpm / 60sec = 1Rps. 1Rps / 6poles =  1sec / 6 = 0.166 ou 166ms
// -- duty cycle 56.7% de 166ms = 94ms high
// -- ratio_low = percent / (100 - percent); // en duty high 56.75%  = ~1.30


// -------- VARIABLES GLOBALES ----------------------

unsigned long start = 0;           // timer

volatile bool button_on = false;  // throttle in use = false au boot 
volatile bool pas_on   = false;   // test interrupt PAS 

bool cruise_on = false;           // cruise controle on-off
volatile bool brake_cut = false;  // signal brake true-false

// -------- MAIN PROG ----------------------


void setup() {
    Serial.begin(115200);
    
    pinMode(LED_PIN, OUTPUT);
    
    pinMode(PAS_PIN, INPUT_PULLUP);      // PAS Hall, without resistor, signal à 1 devant aimant
    
    pinMode(BRAKE_PIN, INPUT_PULLUP);    // Brake pin, without resistor
    
    
    //pinMode(BUTTON_PIN, INPUT_PULLUP); // Pushbutton, without resistor (with input_pullup 1 or high = off !)    
    btn.begin(BUTTON_PIN, INPUT_PULLUP, true);
    
    btn.setClickHandler(handler);
    ////button.setLongClickHandler(handler);    // this will only be called upon release
    btn.setLongClickDetectedHandler(handler);  // this will only be called upon detection
    btn.setDoubleClickHandler(handler);
    //btn.setTripleClickHandler(handler);
    
    //btn.setDoubleClickTime(250); // defaut DOUBLECLICK_MS 300
    
    
    digitalWrite(LED_PIN, LOW);        // signal led/controleur sur Low au boot
    
    // -- Interrupt pedaling : appel "isr_pas" sur signal CHANGE (int. 0, D2) 
    attachInterrupt(digitalPinToInterrupt(PAS_PIN), isr_pas, CHANGE); 
    
    // -- Interrupt brake cut off : appel "isr_brake" sur signal RISING  (int. 1, D3)
    #if BRAKE_MODE == 1
        attachInterrupt(digitalPinToInterrupt(BRAKE_PIN), isr_brake_cut, FALLING); 
    #else
        attachInterrupt(digitalPinToInterrupt(BRAKE_PIN), isr_brake_cut, RISING); 
    #endif
    
    
    // -- initialise timer interrupt, check button all 30ms
    Timer1.initialize(30000);       // 30000us = 30ms
    Timer1.attachInterrupt(isr_button_loop); 
    
    
    // attention, ne pas mettre d'instruction "delay" ici (et dans le loop egalement) !
    // no delay() here !
    
} // endsetup


void loop()
{

    // -- state push button
    int state_button = digitalRead(BUTTON_PIN);
    
    //Serial.println(duty);             // debug duty
    //Serial.println(t1, 5);            // debug ratio
    //Serial.println(ratio_low, 5);     // debug ratio
    
    // juste pour tester l'interrupt PAS ...
    if (pas_on) {
        //Serial.println(" signal changed");
        pas_on = false;
    }
    
    // si bouton appuye on simule un signal PAS
    // 0 or LOW = ON !  (cf input_pullup)
    if (state_button == LOW) button_on = true; 
    else button_on = false;  
    
    //|| cruise_on
    
    
    
    #if USE_CRUISE
        if (button_on || cruise_on)
    #else  
        if (button_on)
    #endif
        
        {
            if (!brake_cut) run_simul(); // run only if no brake cut-off  
        }
        
        // - brake cutoff (if instaled)
        if (brake_cut) {
            digitalWrite(LED_PIN, LOW);
            brake_cut = false; // reset var
            cruise_on = false; // stop cruise
        }
        
        
        // No delay() here !
        
    } // end loop
    
    
    // -------- FUNCTIONS ----------------------
    
    void run_simul() {
        // -- simul PAS, high signal
        start = millis();
        while (millis() - start <= simul_pas) {
            digitalWrite(LED_PIN, HIGH);
        }
        // -- simul PAS, low signal (ratio_low cf constantes)
        start = millis();
        while (millis() - start <= simul_pas / ratio_low) {
            digitalWrite(LED_PIN, LOW);
        }
    } //endfunc
    
    
    // -- interrupt pedaling 
    // - simple replication des signaux du PAS vers le controleur lors du mode pedalage sans appuyer sur le throttle
    void isr_pas() {
        
        pas_on = true;    
        
        // replication du pedalge seulement si le throttle n'est pas en fonction
        if (button_on == false) { 
            
            if (digitalRead(PAS_PIN) == HIGH) digitalWrite(LED_PIN, HIGH);
            else  digitalWrite(LED_PIN, LOW);
            //Serial.println("pedaling..."); // pour debug, eviter si possible un serial dans une interruption !
        } 
        
    } // endfunc
    
    // -- interrupt brake cutt-off 
    void isr_brake_cut() { 
        brake_cut = true;
        //Serial.println(brake_cut);     // debug brake
    }   
    
    
    // -- interrupt, force to check btn.loop
    void isr_button_loop() {
        
        btn.loop();
        
    } // endfunc
    
    
    // -- handler click button
    void handler(Button2& btn) {
        
        switch (btn.getType()) {
            
          case single_click:
            //Serial.println("simple click ");
            cruise_on = false;
            break;
            
          case double_click:
            //Serial.println("double click ");
            cruise_on = !cruise_on;    // toogle  cruise control  
            
            break;
            
            //case triple_click:
            //  Serial.println("triple click ");
            //  break;
            
          case long_click:
            //Serial.println("long click");
            cruise_on = false;
            break;
            
        } //endswitch
        
        //Serial.println(btn.getNumberOfClicks());    
        
    } // endfunc
    
    
    