At app_conf.h :

To set super low power mode, disable debugger and uart:
#define CFG_LPM_LEVEL               (2U)

Simple low power with debugger and uart:
#define CFG_LPM_LEVEL               (1U)