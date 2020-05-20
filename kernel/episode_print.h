/********************************************************************************
	File			: episodefs_util.h
	Description		: Defines for Scene Episode Filesystem utilities
    Author          : @Information
********************************************************************************/
#ifndef	__EPISODE_UTIL_H__
#define	__EPISODE_UTIL_H__


#define	EPISODE_DEBUG

#ifdef	EPISODE_DEBUG
	#define	SCENE_PRINT( msg, args... ) do {										\
		printk( KERN_INFO g, ##args );										\
	} while( 0 )
#else
	#define	SCENEPRINT( msg, args... ) do { } while( 0 )
#endif

#define	SCENE_ERROR( msg, args... ) do {										\
	printk( KERN_ERR msg, ##args );												\
} while( 0 )

#endif	// __EPISODE_UTIL_H__