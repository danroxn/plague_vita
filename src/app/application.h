#ifndef PLAGUE_APP_APPLICATION_H
#define PLAGUE_APP_APPLICATION_H
void port_app_start(void);
void port_app_run(void);
void port_app_loading_start(void);
void port_app_loading_pause(void);
void port_app_loading_draw_dialog(void);
void port_app_loading_resume(void);
void port_app_loading_handover(int dialog_active);
#endif
