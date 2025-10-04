#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define CONFIG_PATH "/config/come pad/config.txt"
#define MAX_KEYS 16
#define MAX_MACROS 8
#define MAX_KEY_NAME_LEN 16
#define MAX_KEY_COMBO_LEN 64

typedef enum {
    ACTIVATION_TYPE_HOLD,
    ACTIVATION_TYPE_TAP,
    ACTIVATION_TYPE_DOUBLE_TAP
} ActivationType;

typedef struct {
    u64 buttons;
    u64 lastPressTime;
    int tapCount;
} KeyState;

typedef struct {
    char name[MAX_KEY_NAME_LEN];
    u64 key;
    double interval; // 秒/20次
    int repeatCount;
    ActivationType activationType;
    u64 activationCombo;
    bool active;
    u64 lastActivationTime;
    int currentRepeat;
} Macro;

static Macro macros[MAX_MACROS];
static int macroCount = 0;
static KeyState keyStates[HidNpadButton_Count];
static bool running = true;
static bool macroActive = false;
static u64 activeMacroIndex = -1;
static double lastMacroTime = 0;

static const char* buttonNames[] = {
    "A", "B", "X", "Y", "LSTICK", "RSTICK", "L", "R", "ZL", "ZR",
    "PLUS", "MINUS", "DLEFT", "DUP", "DRIGHT", "DDOWN", "LSTICK_LEFT",
    "LSTICK_UP", "LSTICK_RIGHT", "LSTICK_DOWN", "RSTICK_LEFT", "RSTICK_UP",
    "RSTICK_RIGHT", "RSTICK_DOWN", "SL", "SR"
};

static const u64 buttonValues[] = {
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
    HidNpadButton_LStick, HidNpadButton_RStick, HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR, HidNpadButton_Plus, HidNpadButton_Minus,
    HidNpadButton_DLeft, HidNpadButton_DUp, HidNpadButton_DRight, HidNpadButton_DDown,
    HidNpadButton_StickLLeft, HidNpadButton_StickLUp, HidNpadButton_StickLRight,
    HidNpadButton_StickLDown, HidNpadButton_StickRLeft, HidNpadButton_StickRUp,
    HidNpadButton_StickRRight, HidNpadButton_StickRDown, HidNpadButton_Sl,
    HidNpadButton_Sr
};

static void initKeyStates() {
    for (int i = 0; i < HidNpadButton_Count; i++) {
        keyStates[i].buttons = buttonValues[i];
        keyStates[i].lastPressTime = 0;
        keyStates[i].tapCount = 0;
    }
}

static void loadConfig() {
    macroCount = 0;
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        // Create default config if not exists
        f = fopen(CONFIG_PATH, "w");
        if (f) {
            fprintf(f, "# Come Pad Configuration\n");
            fprintf(f, "# Format: name,key,interval(repeat/20s),repeatCount,activationType(0=hold,1=tap,2=double_tap),activationCombo\n");
            fprintf(f, "# activationCombo: combination of keys separated by + (e.g., L+R+PLUS)\n");
            fprintf(f, "# Example:\n");
            fprintf(f, "A, A, 0.5, 0, 2, \n");
            fprintf(f, "B, B, 0.3, 100, 1, L+R\n");
            fclose(f);
        }
        // Load default config
        strcpy(macros[0].name, "A");
        macros[0].key = HidNpadButton_A;
        macros[0].interval = 0.5;
        macros[0].repeatCount = 0;
        macros[0].activationType = ACTIVATION_TYPE_DOUBLE_TAP;
        macros[0].activationCombo = 0;
        macros[0].active = false;
        macroCount = 1;
        
        strcpy(macros[1].name, "B");
        macros[1].key = HidNpadButton_B;
        macros[1].interval = 0.3;
        macros[1].repeatCount = 100;
        macros[1].activationType = ACTIVATION_TYPE_TAP;
        macros[1].activationCombo = HidNpadButton_L | HidNpadButton_R;
        macros[1].active = false;
        macroCount = 2;
        return;
    }
    
    char line[256];
    int index = 0;
    while (fgets(line, sizeof(line), f) && index < MAX_MACROS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char name[MAX_KEY_NAME_LEN];
        char keyStr[MAX_KEY_NAME_LEN];
        double interval;
        int repeatCount;
        int activationType;
        char activationComboStr[MAX_KEY_COMBO_LEN];
        
        if (sscanf(line, "%[^,],%[^,],%lf,%d,%d,%[^\n]", 
            name, keyStr, &interval, &repeatCount, &activationType, activationComboStr) == 6) {
            
            // Find key value
            u64 key = 0;
            for (int i = 0; i < HidNpadButton_Count; i++) {
                if (strcasecmp(keyStr, buttonNames[i]) == 0) {
                    key = buttonValues[i];
                    break;
                }
            }
            
            // Parse activation combo
            u64 activationCombo = 0;
            char* token = strtok(activationComboStr, "+ ");
            while (token) {
                for (int i = 0; i < HidNpadButton_Count; i++) {
                    if (strcasecmp(token, buttonNames[i]) == 0) {
                        activationCombo |= buttonValues[i];
                        break;
                    }
                }
                token = strtok(NULL, "+ ");
            }
            
            strncpy(macros[index].name, name, MAX_KEY_NAME_LEN - 1);
            macros[index].name[MAX_KEY_NAME_LEN - 1] = '\0';
            macros[index].key = key;
            macros[index].interval = interval;
            macros[index].repeatCount = repeatCount;
            macros[index].activationType = (ActivationType)activationType;
            macros[index].activationCombo = activationCombo;
            macros[index].active = false;
            index++;
        }
    }
    macroCount = index;
    fclose(f);
}

static void saveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) return;
    
    fprintf(f, "# Come Pad Configuration\n");
    fprintf(f, "# Format: name,key,interval(repeat/20s),repeatCount,activationType(0=hold,1=tap,2=double_tap),activationCombo\n");
    fprintf(f, "# activationCombo: combination of keys separated by + (e.g., L+R+PLUS)\n");
    
    for (int i = 0; i < macroCount; i++) {
        // Find key name
        const char* keyName = "UNKNOWN";
        for (int j = 0; j < HidNpadButton_Count; j++) {
            if (macros[i].key == buttonValues[j]) {
                keyName = buttonNames[j];
                break;
            }
        }
        
        // Build activation combo string
        char activationComboStr[MAX_KEY_COMBO_LEN] = "";
        bool first = true;
        for (int j = 0; j < HidNpadButton_Count; j++) {
            if (macros[i].activationCombo & buttonValues[j]) {
                if (!first) strcat(activationComboStr, "+");
                strcat(activationComboStr, buttonNames[j]);
                first = false;
            }
        }
        
        fprintf(f, "%s,%s,%.2f,%d,%d,%s\n",
            macros[i].name, keyName, macros[i].interval, macros[i].repeatCount,
            macros[i].activationType, activationComboStr);
    }
    
    fclose(f);
}

static void drawUI() {
    consoleClear();
    printf("Come Pad - Switch Key Macro Tool\n");
    printf("===============================\n\n");
    
    printf("Active Macro: %s\n", macroActive ? macros[activeMacroIndex].name : "None");
    if (macroActive) {
        printf("Progress: %d/%d\n", macros[activeMacroIndex].currentRepeat,
               macros[activeMacroIndex].repeatCount ? macros[activeMacroIndex].repeatCount : 9999);
    }
    
    printf("\nMacros:\n");
    for (int i = 0; i < macroCount; i++) {
        printf("%d. %s: Key=%s, Interval=%.2f, Repeat=%d, Type=%d, Activator=",
               i+1, macros[i].name, 
               macros[i].key ? buttonNames[__builtin_ctzll(macros[i].key)] : "None",
               macros[i].interval, macros[i].repeatCount, macros[i].activationType);
        
        // Print activation combo
        bool first = true;
        for (int j = 0; j < HidNpadButton_Count; j++) {
            if (macros[i].activationCombo & buttonValues[j]) {
                if (!first) printf("+");
                printf("%s", buttonNames[j]);
                first = false;
            }
        }
        if (first) printf("None");
        
        if (macros[i].active) printf(" [ACTIVE]");
        printf("\n");
    }
    
    printf("\nControls:\n");
    printf("- Any button (except sticks) to stop active macro\n");
    printf("- Double tap a key to trigger its macro (if set)\n");
    printf("- Press SELECT to exit\n");
    printf("- Press PLUS to save config\n");
}

static void updateKeyStates(u64 buttons, u64 currentTime) {
    for (int i = 0; i < HidNpadButton_Count; i++) {
        if (buttons & buttonValues[i]) {
            if (currentTime - keyStates[i].lastPressTime < 300000000) { // 300ms
                keyStates[i].tapCount++;
            } else {
                keyStates[i].tapCount = 1;
            }
            keyStates[i].lastPressTime = currentTime;
        }
    }
}

static bool checkActivation(Macro* macro, u64 buttons, u64 currentTime) {
    switch (macro->activationType) {
        case ACTIVATION_TYPE_HOLD:
            return (buttons & macro->activationCombo) == macro->activationCombo;
            
        case ACTIVATION_TYPE_TAP:
            for (int i = 0; i < HidNpadButton_Count; i++) {
                if (macro->activationCombo & buttonValues[i]) {
                    if (keyStates[i].tapCount > 0 && 
                        currentTime - keyStates[i].lastPressTime < 300000000) {
                        keyStates[i].tapCount = 0; // Reset tap count
                        return true;
                    }
                }
            }
            return false;
            
        case ACTIVATION_TYPE_DOUBLE_TAP:
            for (int i = 0; i < HidNpadButton_Count; i++) {
                if (macro->key == buttonValues[i] && keyStates[i].tapCount >= 2) {
                    keyStates[i].tapCount = 0; // Reset tap count
                    return true;
                }
            }
            return false;
    }
    return false;
}

static void pressKey(u64 key) {
    hidKeyboardPressKey(HidNpadIdType_Handheld, key);
    svcSleepThread(5000000); // 5ms
    hidKeyboardReleaseKey(HidNpadIdType_Handheld, key);
}

int main(int argc, char* argv[]) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    
    initKeyStates();
    loadConfig();
    
    while (running) {
        padUpdate(&pad);
        u64 buttons = padGetButtonsDown(&pad);
        u64 currentTime = armTicksToNs(armGetSystemTick());
        
        updateKeyStates(buttons, currentTime);
        
        // Check for exit
        if (buttons & HidNpadButton_Minus) {
            running = false;
        }
        
        // Save config
        if (buttons & HidNpadButton_Plus) {
            saveConfig();
        }
        
        // Check for macro activation
        if (!macroActive) {
            for (int i = 0; i < macroCount; i++) {
                if (checkActivation(&macros[i], buttons, currentTime)) {
                    macroActive = true;
                    activeMacroIndex = i;
                    macros[i].active = true;
                    macros[i].currentRepeat = 0;
                    lastMacroTime = currentTime;
                    break;
                }
            }
        } else {
            // Check if any button (except sticks) is pressed to stop macro
            u64 nonStickButtons = buttons & ~(HidNpadButton_LStick | HidNpadButton_RStick | 
                                              HidNpadButton_StickLLeft | HidNpadButton_StickLUp |
                                              HidNpadButton_StickLRight | HidNpadButton_StickLDown |
                                              HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
                                              HidNpadButton_StickRRight | HidNpadButton_StickRDown);
            if (nonStickButtons) {
                macroActive = false;
                macros[activeMacroIndex].active = false;
                activeMacroIndex = -1;
            } else {
                // Execute macro
                double intervalNs = macros[activeMacroIndex].interval * 1000000000.0 / 20.0;
                if (currentTime - lastMacroTime >= intervalNs) {
                    pressKey(macros[activeMacroIndex].key);
                    macros[activeMacroIndex].currentRepeat++;
                    lastMacroTime = currentTime;
                    
                    // Check if macro should stop
                    if (macros[activeMacroIndex].repeatCount > 0 && 
                        macros[activeMacroIndex].currentRepeat >= macros[activeMacroIndex].repeatCount) {
                        macroActive = false;
                        macros[activeMacroIndex].active = false;
                        activeMacroIndex = -1;
                    }
                }
            }
        }
        
        drawUI();
        consoleUpdate(NULL);
        svcSleepThread(10000000); // 10ms
    }
    
    consoleExit(NULL);
    return 0;
}
