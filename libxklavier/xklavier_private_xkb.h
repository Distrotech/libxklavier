#ifndef __XKLAVIER_PRIVATE_XKB_H__
#define __XKLAVIER_PRIVATE_XKB_H__

#include <X11/extensions/XKBrules.h>

extern void _XklStdXkbHandler( int grp, XklStateChange changeType,
                               unsigned inds, Bool setInds );

extern void _XklXkbEvHandler( XkbEvent * kev );

extern void XklDumpXkbDesc( const char *filename, XkbDescPtr kbd );

extern Bool _xklXkbExtPresent;

extern XkbDescPtr _xklXkb;

#define ForPhysIndicators( i, bit ) \
    for ( i=0, bit=1; i<XkbNumIndicators; i++, bit<<=1 ) \
          if ( _xklXkb->indicators->phys_indicators & bit )

extern int _xklXkbEventType, _xklXkbError;

extern XkbRF_VarDefsRec _xklVarDefs;

#endif