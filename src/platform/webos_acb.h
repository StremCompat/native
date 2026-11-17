/* webOS 3 and 4 without exported video windows: the picture of a media-service video reaches the
   screen only once the TV's ACB library (libAcbAPI) ties that media to the main video plane. Without
   it the video plays with sound behind a black picture. */
#ifndef SC_WEBOS_ACB_H
#define SC_WEBOS_ACB_H

/* the library is on this TV (looked up once, at run time) */
int acb_available(void);

/* what the media service said of the picture (its videoInfo event), for the plane */
void acb_video_info(int width, int height, double fps, double bit_rate, const char *hdr_type);

/* the media just loaded: tie it to the plane, step by step on a thread of its own; paused says
   which state to end in. Called again for every new media. */
void acb_bind(const char *media_id, int paused);

/* playing or paused, once bound */
void acb_set_playing(int playing);

/* the video was closed: the plane let go */
void acb_release(void);

#endif
