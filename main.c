/* 
*main.c  
*Simple media player based on Gstreamer and GTK 
*/  
//#include <gst/gst.h>  
#include <gdk/gdkx.h>  
#include <gtk/gtk.h>  
//#include <gst/interfaces/xoverlay.h>  
#include <string.h>  
#include "main.h"  

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avstring.h>
#include <libavutil/opt.h>
#include <libavdevice/avdevice.h>

#include <libavformat/avio.h>
#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/avstring.h"

#include <semaphore.h>
#include <pthread.h>
#include "SDL2/SDL.h"  

  
static GtkWidget *main_window;  
static GtkWidget *play_button;  
static GtkWidget *pause_button;  
static GtkWidget *stop_button;  
static GtkWidget *status_label;  
static GtkWidget *time_label;  
static GtkWidget *seek_scale;  
static GtkWidget *video_output;  
//static gpointer window;  
  
//static GstElement *play = NULL;  
//static GstElement *bin;  
  
//static guint timeout_source = 0;  
static char *current_filename = NULL;  
gboolean no_seek = FALSE;  

// 全局变量定义
static void *g_hplayer = NULL;

//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)  
  
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)  
  
int thread_exit=0;  
int thread_pause=0;  
pthread_t pthread_player_open;
int sfp_refresh_thread(void *opaque)
{  
    thread_exit=0;  
    thread_pause=0;  
  
    while (!thread_exit) 
	{  
        if(!thread_pause)
		{  
            SDL_Event event;  
            event.type = SFM_REFRESH_EVENT;  
            SDL_PushEvent(&event);  
        }  
        SDL_Delay(25);  
    }  
    thread_exit=0;  
    thread_pause=0;  
    //Break  
    SDL_Event event;  
    event.type = SFM_BREAK_EVENT;  
    SDL_PushEvent(&event);  
  
    return 0;  
}  

void pktqueue_destroy(PKTQUEUE *ppq)
{
    // free
    if (ppq->bpkts) free(ppq->bpkts);
    if (ppq->fpkts) free(ppq->fpkts);
    if (ppq->apkts) free(ppq->apkts);
    if (ppq->vpkts) free(ppq->vpkts);

    // close
    sem_destroy(&(ppq->fsemr));
    sem_destroy(&(ppq->asemr));
    sem_destroy(&(ppq->asemw));
    sem_destroy(&(ppq->vsemr));
    sem_destroy(&(ppq->vsemw));

    // clear members
    memset(ppq, 0, sizeof(PKTQUEUE));
}

// 函数实现
gboolean pktqueue_create(PKTQUEUE *ppq)
{
    int i;

    // default size
    if (ppq->asize == 0) ppq->asize = DEF_PKT_QUEUE_ASIZE;
    if (ppq->vsize == 0) ppq->vsize = DEF_PKT_QUEUE_VSIZE;
    ppq->fsize = ppq->asize + ppq->vsize;

    // alloc buffer & semaphore
    ppq->bpkts = (AVPacket* )malloc(ppq->fsize * sizeof(AVPacket ));
    ppq->fpkts = (AVPacket**)malloc(ppq->fsize * sizeof(AVPacket*));
    ppq->apkts = (AVPacket**)malloc(ppq->asize * sizeof(AVPacket*));
    ppq->vpkts = (AVPacket**)malloc(ppq->vsize * sizeof(AVPacket*));
    sem_init(&(ppq->fsemr), 0, ppq->fsize);
    sem_init(&(ppq->asemr), 0, 0         );
    sem_init(&(ppq->asemw), 0, ppq->asize);
    sem_init(&(ppq->vsemr), 0, 0         );
    sem_init(&(ppq->vsemw), 0, ppq->vsize);

    // check invalid
    if (!ppq->bpkts || !ppq->fpkts || !ppq->apkts || !ppq->vpkts) {
        pktqueue_destroy(ppq);
        return FALSE;
    }

    // clear packets
    memset(ppq->bpkts, 0, ppq->fsize * sizeof(AVPacket ));
    memset(ppq->apkts, 0, ppq->asize * sizeof(AVPacket*));
    memset(ppq->vpkts, 0, ppq->vsize * sizeof(AVPacket*));

    // init fpkts
    for (i=0; i<ppq->fsize; i++) {
        ppq->fpkts[i] = &(ppq->bpkts[i]);
    }
    return TRUE;
}

  
 // 函数实现
void* playeropen(char *file)
{
    PLAYER        *player   = NULL;
    AVCodec       *pAVCodec = NULL;
	AVFrame *pFrame,*pFrameYUV;  
	AVPacket *packet;  
    int            vformat  = 0;
    int            width    = 0;
    int            height   = 0;
    AVRational     vrate    = {1, 1};
    uint64_t       alayout  = 0;
    int            aformat  = 0;
    int            arate    = 0;
    uint32_t       i        = 0;
	uint8_t *out_buffer;  
	struct SwsContext *img_convert_ctx;  
	int ret, got_picture;  
	
	//SDL---------------------------  
    int screen_w=0,screen_h=0;  
    SDL_Window *screen;   
    SDL_Renderer* sdlRenderer;  
    SDL_Texture* sdlTexture;  
    SDL_Rect sdlRect; 
	SDL_Thread *video_tid;  
    SDL_Event event;  

	g_print("playeropen\n"); 
    // av register all
	g_print("av_register_all success\n"); 
    av_register_all();
	
	// alloc player context
    player = (PLAYER*)malloc(sizeof(PLAYER));
    memset(player, 0, sizeof(PLAYER));
	
	// create packet queue
    pktqueue_create(&(player->PacketQueue));
	
	// open input file
	if (avformat_open_input(&(player->pAVFormatContext), file, NULL, 0) != 0)
	{
		goto error_handler;
	}
	else
	{
		g_print("avformat_open_input success\n"); 
	}
	
	// find stream info
    if (avformat_find_stream_info(player->pAVFormatContext, NULL) < 0) {
        goto error_handler;
    }
	else
	{
		g_print("avformat_find_stream_info success\n"); 
	}
	
	// get video & audio codec context
	g_print("get video & audio codec context\n"); 
	player->iAudioStreamIndex = -1;
	player->iVideoStreamIndex = -1;
	for (i=0; i<player->pAVFormatContext->nb_streams; i++)
	{
		switch (player->pAVFormatContext->streams[i]->codec->codec_type)
		{
			case AVMEDIA_TYPE_AUDIO:
				g_print("get video & audio codec context:AVMEDIA_TYPE_AUDIO\n");
				g_print("get video & audio codec context:player->iAudioStreamIndex  = %d\n",i);
				player->iAudioStreamIndex  = i;
				player->pAudioCodecContext = player->pAVFormatContext->streams[i]->codec;
				player->dAudioTimeBase     = av_q2d(player->pAVFormatContext->streams[i]->time_base) * 1000;
			break;

			case AVMEDIA_TYPE_VIDEO:
				g_print("get video & audio codec context:AVMEDIA_TYPE_VIDEO\n");
				g_print("get video & audio codec context:player->iVideoStreamIndex  = %d\n",i);
				player->iVideoStreamIndex  = i;
				player->pVideoCodecContext = player->pAVFormatContext->streams[i]->codec;
				player->dVideoTimeBase     = av_q2d(player->pAVFormatContext->streams[i]->time_base) * 1000;
				vrate = player->pAVFormatContext->streams[i]->r_frame_rate;
			break;
			default:
				break;
		}
	}
	
	// open audio codec
	g_print("open audio codec\n"); 
    if (player->iAudioStreamIndex != -1)
    {
        pAVCodec = avcodec_find_decoder(player->pAudioCodecContext->codec_id);
        if (pAVCodec)
        {
			g_print("avcodec_find_decoder  audio codec success\n"); 
            if (avcodec_open2(player->pAudioCodecContext, pAVCodec, NULL) < 0)
            {
                player->iAudioStreamIndex = -1;
            }
			else
			{
				g_print("avcodec_open2  audio codec success\n"); 
			}
        }
        else player->iAudioStreamIndex = -1;
    }

	 // open video codec
	g_print("open video codec\n"); 
    if (player->iVideoStreamIndex != -1)
    {
        pAVCodec = avcodec_find_decoder(player->pVideoCodecContext->codec_id);
        if (pAVCodec)
        {
			g_print("avcodec_find_decoder  video codec success\n"); 
            if (avcodec_open2(player->pVideoCodecContext, pAVCodec, NULL) < 0)
            {
                player->iVideoStreamIndex = -1;
            }
			else
			{
				g_print("avcodec_open2  video codec success\n"); 
			}
        }
        else player->iVideoStreamIndex = -1;
    }
	
	// for video
    if (player->iVideoStreamIndex != -1)
    {
		vformat = player->pVideoCodecContext->pix_fmt;
		width   = player->pVideoCodecContext->width;
		height  = player->pVideoCodecContext->height;
		g_print("player->pVideoCodecContext->pix_fmt = %d\n",vformat);
		g_print("player->pVideoCodecContext->width = %d\n",width);
		g_print("player->pVideoCodecContext->height = %d\n",height);
    }

	// for audio
    if (player->iVideoStreamIndex != -1)
    {
		alayout = player->pAudioCodecContext->channel_layout;
        aformat = player->pAudioCodecContext->sample_fmt;
        arate   = player->pAudioCodecContext->sample_rate;
		g_print("player->pAudioCodecContext->channel_layout = %lu\n",alayout);
		g_print("player->pAudioCodecContext->sample_fmt = %d\n",aformat);
		g_print("player->pAudioCodecContext->sample_rate = %d\n",arate);
    }
	
	pFrame=av_frame_alloc();  
    pFrameYUV=av_frame_alloc();
	out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, width, height));  
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, width, height);  
	packet=(AVPacket *)av_malloc(sizeof(AVPacket));  
	//Output Info-----------------------------  
	printf("--------------- File Information ----------------\n");  
	av_dump_format(player->pAVFormatContext,0,file,0);  
	printf("-------------------------------------------------\n"); 
	img_convert_ctx = sws_getContext(width, height, vformat,   
        width, height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);   
		
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
	{    
		printf( "Could not initialize SDL - %s\n", SDL_GetError());   
		return NULL;  
	}   
	else
	{
		printf( "SDL: SDL_Init success\n");   
	}
	screen_w = player->pVideoCodecContext->width;  
    screen_h = player->pVideoCodecContext->height; 
	//SDL 2.0 Support for multiple windows  
    screen = SDL_CreateWindow("ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,  
        screen_w, screen_h,  
        SDL_WINDOW_OPENGL); 
	if(!screen)
	{    
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());    
        return NULL;  
    }  
	else
	{
	    printf("SDL: SDL_CreateWindow success\n");    
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);  
	if(!sdlRenderer)
	{    
        printf("SDL: could not create Renderer - exiting:%s\n",SDL_GetError());    
        return NULL;  
    }  
	else
	{
	    printf("SDL: SDL_CreateRenderer success\n");    
	}	
    //IYUV: Y + U + V  (3 planes)  
    //YV12: Y + V + U  (3 planes)  
    sdlTexture = SDL_CreateTexture(sdlRenderer, 
								   SDL_PIXELFORMAT_IYUV, 
								   SDL_TEXTUREACCESS_STREAMING,
								   player->pVideoCodecContext->width,
								   player->pVideoCodecContext->height);
	if(!sdlTexture)
	{    
        printf("SDL: could not create Texture - exiting:%s\n",SDL_GetError());    
        return NULL;  
    }  
	else
	{
	    printf("SDL: SDL_CreateTexture success\n");    
	}				

	sdlRect.x=0;  
    sdlRect.y=0;  
    sdlRect.w=screen_w;  
    sdlRect.h=screen_h;  
	
	video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);  
	//------------SDL End------------  
    //Event Loop  
	
	for (;;) 
	{  
        //Wait  
        SDL_WaitEvent(&event);  
        if(event.type==SFM_REFRESH_EVENT)
		{  
            //------------------------------  
            if(av_read_frame(player->pAVFormatContext, packet)>=0)
			{  
                if(packet->stream_index==player->iVideoStreamIndex)
				{  
                    ret = avcodec_decode_video2(player->pVideoCodecContext, pFrame, &got_picture, packet);  
                    if(ret < 0)
					{  
                        printf("avcodec_decode_video2 Error.\n");  
                        return NULL;  
                    }  
                    if(got_picture)
					{  
                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, player->pVideoCodecContext->height, pFrameYUV->data, pFrameYUV->linesize);  
                        //SDL---------------------------  
                        SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );    
                        SDL_RenderClear( sdlRenderer );    
                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );    
                        SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);    
                        SDL_RenderPresent( sdlRenderer );    
                        //SDL End-----------------------  
                    }  
                }  
                av_free_packet(packet);  
            }
			else
			{  
                //Exit Thread  
                thread_exit=1;  
            }  
        }
		else if(event.type==SDL_KEYDOWN)
		{  
            //Pause  
            if(event.key.keysym.sym==SDLK_SPACE)  
                thread_pause=!thread_pause;  
        }
		else if(event.type==SDL_QUIT)
		{  
            thread_exit=1;  
        }
		else if(event.type==SFM_BREAK_EVENT)
		{  
            break;  
        }  
  
    }  
	sws_freeContext(img_convert_ctx);  
	
    printf("SDL: SDL_Quit success\n"); 
    SDL_Quit();  
    //--------------  
    av_frame_free(&pFrameYUV);  
    av_frame_free(&pFrame);  
	avcodec_close(player->pVideoCodecContext);  
    avformat_close_input(&player->pAVFormatContext);  
	
	return player;
	error_handler:
		//playerclose(player);
		return NULL;
}

 
 
// 打开文件  
static void file_open(GtkAction *action)  
{  
	g_print("file_open\n"); 
	GtkWidget *file_chooser = gtk_file_chooser_dialog_new(  
        "Open File", GTK_WINDOW(main_window),  
        GTK_FILE_CHOOSER_ACTION_OPEN,  
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,  
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,  
        NULL);  
  
    if (gtk_dialog_run(GTK_DIALOG(file_chooser)) == GTK_RESPONSE_ACCEPT) 
	{  
		char *filename;  
		filename = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(file_chooser));  
		// g_signal_emit_by_name(G_OBJECT(stop_button), "clicked");  
		if (current_filename) g_free(current_filename);  
		current_filename = filename;  

		// player open file pthread
		pthread_create(&pthread_player_open,NULL,playeropen,current_filename);
		gtk_widget_set_sensitive(GTK_WIDGET(play_button), TRUE);  
	}
	gtk_widget_destroy(file_chooser);
}  
// 退出  
static void file_quit(GtkAction *action)  
{  
    gtk_main_quit();  
}  
// 关于  
static void help_about(GtkAction *action)  
{  
	g_print("help_about\n"); 
	GtkWidget *about_dialog = gtk_about_dialog_new();  
	gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(about_dialog), "FFMPEG Player");  
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about_dialog), "0.0.0");  
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about_dialog), "Copyright @ 2015, OS-easy");  

	gtk_dialog_run(GTK_DIALOG(about_dialog));  
	gtk_widget_destroy(about_dialog);  
}  
  
static GtkActionEntry mainwindow_action_entries[] = {  
    { "FileMenu", "NULL", "文件" },  
    {  
        "OpenFile",  
        GTK_STOCK_OPEN,  
        "打开(O)",  
        "<control>O",  
        "Open a file for playback",  
        G_CALLBACK(file_open)  
    },  
    {  
        "QuitPlayer",  
        GTK_STOCK_QUIT,  
        "退出(Q)",  
        "<control>Q",  
        "Quit the media player",  
        G_CALLBACK(file_quit)  
    },  
    
    { "HelpMenu", "NULL", "帮助" },  
    {  
        "HelpAbout",  
        GTK_STOCK_ABOUT,  
        "关于",  
        "",  
        "About the media player",  
        G_CALLBACK(help_about)  
    }  
};  
  
static void play_clicked(GtkWidget *widget, gpointer data)  
{  
	g_print("play_clicked\n"); 
	if (current_filename) 
	{  
		if (play_file())
		{  
			gtk_widget_set_sensitive(GTK_WIDGET(stop_button), TRUE);  
			gtk_widget_set_sensitive(GTK_WIDGET(pause_button), TRUE);  
			// g_print("Play was pressed\n");    
			gui_status_update(STATE_PLAY);  
			g_print("Play\n");    
		}  
		else 
		{  
			g_print("Failed to play\n");  
		}  
	}  
}  
  
static void pause_clicked(GtkWidget *widget, gpointer data)  
{           
	g_print("pause_clicked\n");               
}  
  
static void stop_clicked(GtkWidget *widget, gpointer data)  
{        
	g_print("stop_clicked\n");   
}  
  
/* Handler for user moving seek bar */  
static void seek_value_changed(GtkRange *range, gpointer data)  
{  
	g_print("seek_value_changed\n");     
}  
  
GtkWidget *build_gui()  
{  
    GtkWidget *main_vbox;  
    GtkWidget *status_hbox;  
    GtkWidget *controls_hbox;  
   // GtkWidget *saturation_controls_hbox;  
  
    GtkActionGroup *actiongroup;  
    GtkUIManager *ui_manager;  
  
    actiongroup = gtk_action_group_new("MainwindowActiongroup");  
    gtk_action_group_add_actions(actiongroup,  
        mainwindow_action_entries,  
        G_N_ELEMENTS(mainwindow_action_entries),  
        NULL);  
  
    ui_manager = gtk_ui_manager_new();  
    gtk_ui_manager_insert_action_group(ui_manager, actiongroup, 0);  
    gtk_ui_manager_add_ui_from_string(  
        ui_manager,  
        "<ui>"  
        "    <menubar name='MainMenu'>"  
        "        <menu action='FileMenu'>"  
        "            <menuitem action='OpenFile'/>"  
        "            <separator name='fsep1'/>"  
        "            <menuitem action='QuitPlayer'/>"  
        "        </menu>"  
        "        <menu action='HelpMenu'>"  
        "            <menuitem action='HelpAbout'/>"  
        "        </menu>"         
        "    </menubar>"  
        "</ui>",  
        -1,  
        NULL);  
      
  
    // 创建主 GtkVBOx. 其他所有都在它里面  
    // 0：各个构件高度可能不同，6：构件之间的间距为6 像素  
    main_vbox = gtk_vbox_new(0, 0);  
  
    // 添加菜单栏  
    gtk_box_pack_start(  
        GTK_BOX(main_vbox),  
        gtk_ui_manager_get_widget(ui_manager, "/ui/MainMenu"),  
        FALSE, FALSE, 0);  
  
    
    // 视频显示区域 
    video_output = gtk_drawing_area_new (); 
    gtk_box_pack_start (GTK_BOX (main_vbox), video_output, TRUE, TRUE, 0); 

 
    // 滑动条控制  
    seek_scale = gtk_hscale_new_with_range(0, 100, 1);  
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);  
    gtk_range_set_update_policy(GTK_RANGE(seek_scale), GTK_UPDATE_DISCONTINUOUS);  
    g_signal_connect(G_OBJECT(seek_scale), "value-changed", G_CALLBACK(seek_value_changed), NULL);  
    gtk_box_pack_start(GTK_BOX(main_vbox), seek_scale, FALSE, FALSE, 0);  
  
    // controls_hbox  
    controls_hbox = gtk_hbox_new(TRUE, 6);  
    gtk_box_pack_start_defaults(GTK_BOX(main_vbox), controls_hbox);  
  
    // 播放按钮  
    play_button = gtk_button_new_from_stock(GTK_STOCK_MEDIA_PLAY);  
    // 设置“敏感”属性，FALSE 表示为灰色，不响应鼠标键盘事件  
    gtk_widget_set_sensitive(play_button, FALSE);  
    g_signal_connect(G_OBJECT(play_button), "clicked", G_CALLBACK(play_clicked), NULL);  
    gtk_box_pack_start_defaults(GTK_BOX(controls_hbox), play_button);  
      
    // 暂停按钮,为使按下时停留在按下状态，使用GtkToggleButton  
    pause_button = gtk_toggle_button_new_with_label(GTK_STOCK_MEDIA_PAUSE);  
    // 将按钮设置为固化按钮  
    gtk_button_set_use_stock(GTK_BUTTON(pause_button), TRUE);  
    gtk_widget_set_sensitive(pause_button, FALSE);  
    g_signal_connect(G_OBJECT(pause_button), "clicked", G_CALLBACK(pause_clicked), NULL);  
    gtk_box_pack_start_defaults(GTK_BOX(controls_hbox), pause_button);  
  
    // 停止按钮  
    stop_button = gtk_button_new_from_stock(GTK_STOCK_MEDIA_STOP);  
    gtk_widget_set_sensitive(stop_button, FALSE);  
    g_signal_connect(G_OBJECT(stop_button), "clicked", G_CALLBACK(stop_clicked), NULL);  
    gtk_box_pack_start_defaults(GTK_BOX(controls_hbox), stop_button);       
      
    // status_hbox  
    status_hbox = gtk_hbox_new(TRUE, 0);  
    gtk_box_pack_start(GTK_BOX(main_vbox), status_hbox, FALSE, FALSE, 0);  
    // 状态标签  
    status_label = gtk_label_new("<b>已停止</b>");  
    gtk_label_set_use_markup(GTK_LABEL(status_label), TRUE);  
    gtk_misc_set_alignment(GTK_MISC(status_label), 0.0, 0.5);  
    gtk_box_pack_start(GTK_BOX(status_hbox), status_label, TRUE, TRUE, 0);  
    // 时间标签     
    time_label = gtk_label_new("00:00:00");  
    gtk_misc_set_alignment(GTK_MISC(time_label), 0.5, 1.0);  
    gtk_box_pack_start(GTK_BOX(status_hbox), time_label, TRUE, TRUE, 0);  
     
    return main_vbox;  
}  
  
/* 
void stop_playback() 
{ 
    if (timeout_source) 
        g_source_remove(timeout_source); 
    timeout_source = 0; 
 
    if (play) { 
        gst_element_set_state(play, GST_STATE_NULL); 
        gst_object_unref(GST_OBJECT(play)); 
        play = NULL; 
    } 
    gui_status_update(STATE_STOP); 
}*/  
  
// destory main window  
static void destroy(GtkWidget *widget, gpointer data)  
{  
    gtk_main_quit();  
}  
// 更新播放时间  
void gui_update_time(const gchar *time, const gint64 position, const gint64 length)  
{  
	g_print("gui_update_time\n");  
}  
// 更新播放状态  
void gui_status_update(PlayerState state)  
{  
    g_print("gui_status_update\n");  
}  
/*  
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data)  
{  
   g_print("bus_callback\n");  
}  
*/
 /* 
static gboolean build_gstreamer_pipeline(const gchar *uri)  
{  
   g_print("build_gstreamer_pipeline\n");  
   return TRUE;
}  
*/
/*
// load file to play  
gboolean load_file(const gchar *uri)  
{  
	g_print("load_file\n"); 
	return TRUE;
}  
*/
/*
static gboolean update_time_callback(GstElement *pipeline)  
{  
	g_print("update_time_callback\n");  
}  
 */
gboolean play_file()  
{  
	g_print("play_file\n"); 
	return TRUE;
}  
  
/* Attempt to seek to the given percentage through the file */  
void seek_to(gdouble percentage)  
{  
	g_print("seek_to\n");  
}  
  
int main(int argc, char *argv[])  
{  
    // 初始化 GTK+  
    gtk_init(&argc, &argv);  
    //gst_init(&argc, &argv);  
    // 创建窗口  
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);  
    // 设置窗口标题  
    gtk_window_set_title(GTK_WINDOW(main_window), "FFMPEG Player");  
    // 主窗口销毁句柄  
    g_signal_connect(G_OBJECT(main_window), "destroy", G_CALLBACK(destroy), NULL);  
    // 创建主窗口GUI  
    gtk_container_add(GTK_CONTAINER(main_window), build_gui());  
    // 显示  
    gtk_widget_show_all(GTK_WIDGET(main_window));     
    // 开始主循环  
    gtk_main();  
  
    return 0;  
}  


