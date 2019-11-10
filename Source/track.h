//HEADER_GOES_HERE
#ifndef __TRACK_H__
#define __TRACK_H__

void track_process();
void track_lmb_loc(BYTE bCmd, BYTE x, BYTE y);
void track_lmb_param1(BYTE bCmd, WORD wParam1);
void track_repeat_walk(BOOL rep);
BOOL track_isscrolling();

#endif /* __TRACK_H__ */
