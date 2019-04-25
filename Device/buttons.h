#ifndef BUTTONS_H
#define BUTTONS_H

class ButtonManager
{
  private:
    int lastButtonAState;
    int lastButtonBState;

  public:
    enum ButtonStates
    {
      None = 0,
      ButtonAPressed = 1,
      ButtonBPressed = 2
    };

    void init()
    {
      // initialize the button pin as a input
      pinMode(USER_BUTTON_A, INPUT);
      lastButtonAState = digitalRead(USER_BUTTON_A);

      // initialize the button B pin as a input
      pinMode(USER_BUTTON_B, INPUT);
      lastButtonBState = digitalRead(USER_BUTTON_B);
    }

    int read()
    {          
      int result = ButtonStates::None;
      int buttonAState = digitalRead(USER_BUTTON_A);
      int buttonBState = digitalRead(USER_BUTTON_B);
      
      if (buttonAState == LOW && lastButtonAState == HIGH)
      {
        result |= ButtonStates::ButtonAPressed;
      }
      if (buttonBState == LOW && lastButtonBState == HIGH)
      {
        result |= ButtonStates::ButtonBPressed;
      }
      lastButtonAState = buttonAState;
      lastButtonBState = buttonBState;
      return result;
    }
};

#endif