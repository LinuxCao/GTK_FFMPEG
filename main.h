/* 
 * main.h 
 */  
  
#ifndef MAIN_H  
#define MAIN_H  
  
#include <gtk/gtk.h>  
  
 typedef enum {  
    STATE_STOP,  
    STATE_PLAY,  
    STATE_PAUSE  
} PlayerState; 
 
void gui_status_update(PlayerState state);  
void gui_update_time(const gchar *time, const gint64 position, const gint64 length);  
   
gboolean load_file(const gchar *uri);  
void seek_to(gdouble percentage);  
gboolean play_file();
  
#endif 