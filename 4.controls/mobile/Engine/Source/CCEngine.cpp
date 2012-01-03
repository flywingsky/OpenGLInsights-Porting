/*-----------------------------------------------------------
 * 2c - Cross Platform 3D Application Framework
 *-----------------------------------------------------------
 * Copyright © 2010 – 2011 France Telecom
 * This software is distributed under the Apache 2.0 license,
 * see the "license.txt" file for more details.
 *-----------------------------------------------------------
 * File Name   : CCEngine.cpp
 *-----------------------------------------------------------
 */

#include "CCDefines.h"
#include "CCTextureFontPage.h"
#include "CCObjects.h"
#include "CCSceneBase.h"
#include "CCFileManager.h"

#include "CCDeviceControls.h"
#include "CCDeviceRenderer.h"
#include "CCGLView.h"


CCEngine::CCEngine()
{
	renderFlags = render_all;
	fpsLimit = 1/50.0f;
    
    // Initialise our gameTime lastUpdate value;
    updateTime();
}


CCEngine::~CCEngine()
{		
	scenes.deleteObjectsAndList( true );
	cameras.deleteObjectsAndList();
	
    delete urlManager;
	delete textureManager;
	delete controls;
	delete renderer;
	delete collisionManager;

	gEngine = NULL;
	
    CCEngineThreadUnlock();
}


void CCEngine::setupNativeThread()
{
	urlManager = new CCURLManager();
}


static int zCompare(const void *a, const void *b)
{
    const CCSceneCollideable *objectA = CCOctreeGetVisibleCollideables( *(int*)a );
    const CCSceneCollideable *objectB = CCOctreeGetVisibleCollideables( *(int*)b );
    
    if( objectA->drawOrder == 200 || objectB->drawOrder == 200 )
    {
        if( objectA->drawOrder == 200 && objectB->drawOrder == 200 )
        {
            const CCVector3 &cameraPosition = gEngine->currentCamera->getRotatedPosition();
            const CCVector3 *positionA = objectA->positionPtr;
            const CCVector3 *positionB = objectB->positionPtr;
            const float distanceA = CCVector3Distance( *positionA, cameraPosition, true );
            const float distanceB = CCVector3Distance( *positionB, cameraPosition, true );
            
            // If A is smaller than B, swap
            return distanceB - distanceA;
        }
        
        // Largest to the back to be drawn last
        return objectA->drawOrder - objectB->drawOrder;
    }
    
    // Largest to the back to be drawn last
    return objectA->drawOrder - objectB->drawOrder;
    
    return 0;
}


void CCEngine::setupEngineThread()
{
    renderer = new CCDeviceRenderer();
    renderer->setup( false, true );
    DEBUG_OPENGL();
    
    textureManager = new CCTextureManager();
    textureManager->load();
    DEBUG_OPENGL();
    textureManager->loadFont( "HelveticaNeueLight" );
    //textureManager->loadFont( "HelveticaNeueBold" );
    DEBUG_OPENGL();
    
    controls = new CCDeviceControls();
    
	currentCamera = NULL;
    
    collisionManager = new CCCollisionManager( 250000.0f );
    
	CCOctreeSetRenderSortCallback( &zCompare );
    
    renderer->setupOpenGL();
    DEBUG_OPENGL();
    
    // Start Game
    start();
}


void CCEngine::addCamera(CCCameraBase *camera, const int index)
{
    camera->setIndex( cameras.length );
    cameras.add( camera );

    if( index != -1 )
    {
        cameras.reinsert( camera, index );
    }
}


const bool CCEngine::removeCamera(CCCameraBase *camera)
{
	if( cameras.remove( camera ) )
	{
		return true;
	}
	return false;
}


void CCEngine::refreshCameras()
{;
    for( int i=0; i<cameras.length; ++i )
    {
        CCCameraBase *camera = cameras.list[i];
        camera->refreshViewport();
    }
}


void CCEngine::addScene(CCSceneBase *scene, const int index)
{
	scenes.add( scene );
	scene->setup();
    
    if( index != -1 )
    {
        scenes.reinsert( scene, index );
    }
}


void CCEngine::removeScene(CCSceneBase* scene)
{
	if( scenes.remove( scene ) )
	{
		DELETE_OBJECT( scene );
	}
    else
    {
        ASSERT( false );
    }
}


const double getSystemTime()
{
#ifdef IOS
    
    const NSTimeInterval currentTime = [NSDate timeIntervalSinceReferenceDate];
    
#elif defined( QT )
    
    const double currentTime = gView->timer.elapsed() * 0.001;
    
#elif defined( ANDROID )
    
    struct timespec res;
	clock_gettime( CLOCK_REALTIME, &res );
	const double currentTime = res.tv_sec + (double)( ( res.tv_nsec / 1e6 ) * 0.001 );

#endif

    return currentTime;
}


const bool CCEngine::updateTime()
{
    double currentTime = getSystemTime();
    gameTime.real = (float)( currentTime - gameTime.lastUpdate );
	
    // If we're too fast, sleep
	while( gameTime.real < fpsLimit )
	{
        const uint difference = roundf( ( fpsLimit - gameTime.real ) * 1000.0f ) + 1;
		usleep( difference );

		currentTime = getSystemTime();
		gameTime.real = (float)( currentTime - gameTime.lastUpdate );
	}
	
	// Fake 25 fps
	gameTime.delta = MIN( gameTime.real, 0.04f );

    gameTime.lastUpdate = currentTime;
    
    return true;
}


const bool CCEngine::updateNativeThread()
{	
    // Run callbacks
	if( nativeThreadCallbacks.length > 0 )
    {
        CCEngineThreadLock();
        for( int i=0; i<nativeThreadCallbacks.length; ++i )
        {
            nativeThreadCallbacks.list[i]->run();
            delete nativeThreadCallbacks.list[i];
        }
        nativeThreadCallbacks.length = 0;
        CCEngineThreadUnlock();
    }

    urlManager->updateNativeThread();
	return false;
}


void CCEngine::updateEngineThread()
{	
    // Update our system time
    if( updateTime() == false )
    {
#ifdef ANDROID
    	// FIXME: Android needs to always redraw the scene
    	// We currently never return false, so perhaps remove this..
    	renderer->clear();
        renderLoop();
#endif
        return;
    }

	gameTime.lifetime += gameTime.real;

#if LOG_FPS
    static uint loggedUpdates = 0;
    static float loggedDelta = 0.0f;
    loggedUpdates++;
    loggedDelta += gameTime.real;
    if( loggedDelta > 1.0f )
    {
        const float averageFPS = 1.0f / ( loggedDelta / loggedUpdates );
        DEBUGLOG( "Average FPS: %f \n", averageFPS );
        loggedUpdates = 0;
        loggedDelta = 0.0f;
    }
#endif
    
    // Run callbacks
    {
        CCEngineThreadLock();
        for( int i=0; i<engineThreadCallbacks.length; ++i )
        {
            engineThreadCallbacks.list[i]->run();
            delete engineThreadCallbacks.list[i];
        }
        engineThreadCallbacks.length = 0;
        CCEngineThreadUnlock();
    }
	
    finishJobs();
	updateLoop();
	
    renderer->update( gameTime.delta );
	renderer->clear();
    renderLoop();
	renderer->render();
	
#if defined DEBUGON && TARGET_IPHONE_SIMULATOR
	// 66 frames a second in debug
	//usleep( 15000 );
	usleep( 0 );
#endif
}


void CCEngine::updateLoop()
{	
#if defined PROFILEON
    CCProfiler profile( "CCEngine::updateGame()" );
#endif
    
    // Remove any redundant scenes
    for( int i=0; i<scenes.length; ++i )
	{
		CCSceneBase *scene = scenes.list[i];
        if( scene->shouldDelete() )
		{
            removeScene( scene );
			i--;
		}
	}
    
    CCEngineThreadLock();
    for( int i=0; i<cameras.length; ++i )
    {
        CCCameraBase *camera = cameras.list[i];
        camera->updateControls();
    }
	controls->update( gameTime );
    CCEngineThreadUnlock();
    
    // Allow scene to handle the controls first
	for( int i=0; i<scenes.length; ++i )
    {
        if( scenes.list[i]->handleControls( gameTime ) )
        {
            break;
        }
	}

    // Run through our controllers first
    for( int i=0; i<updaters.length; ++i )
    {
        // Delete on finish update
        CCUpdater *updater = updaters.list[i];
        if( updater->update( gameTime.delta ) == false )
        {
            updaters.remove( updater );
            DELETE_OBJECT( updater );
        }
    }
	
    // Update scene logic
	for( int i=0; i<scenes.length; ++i )
    {
        CCSceneBase *scene = scenes.list[i];
        scene->update( gameTime );
	}
}


void CCEngine::renderLoop()
{	
#if defined PROFILEON
    CCProfiler profile( "CCEngine::render()" );
#endif
    
    // Tell the texture manager we're rendering a new frame
    gEngine->textureManager->prepareRender();
    for( int i=0; i<collisionManager->length; ++i )
    {
        collisionManager->objects[i]->visible = false;
    }

	for( int cameraIndex=0; cameraIndex<cameras.length; ++cameraIndex )
    {
		currentCamera = cameras.list[cameraIndex];
		if( currentCamera->enabled == false )
		{
			continue;
		}


#if defined PROFILEON
		CCProfiler profile( "CCEngine::render()::camera" );
#endif

        currentCamera->setViewport();
		
		// 2D Background
		{	
			CCMatrix matrix;
			CCMatrixLoadIdentity( matrix );
			CCMatrixRotate( matrix, renderer->orientation.current, 0.0f, 0.0f, 1.0f );
			CCMatrixOrtho( matrix, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f );
			GLSetPushMatrix( matrix );
			
			for( int i=0; i<scenes.length; ++i )
			{
				scenes.list[i]->render2DBackground( cameraIndex );
			}
		}
		
		// 3D Rendering
		glEnable( GL_DEPTH_TEST );
		extern bool gUseProjectionMatrix;
		gUseProjectionMatrix = true;
		{	
			currentCamera->update();
			
			for( uint pass=render_background; pass<render_finished; ++pass )
			{
#if defined PROFILEON
				CCProfiler profile( "CCEngine::render()::pass" );
#endif

				// Render all the non-collideables
				glDisable( GL_BLEND );
				for( int i=0; i<scenes.length; ++i )
				{
					scenes.list[i]->render( currentCamera, pass, false );
				}
				
				// Render all the visible collideables
				{
#if defined PROFILEON
					CCProfiler profile( "CCOctreeRenderVisibleObjects( false )" );
#endif
					CCOctreeRenderVisibleObjects( currentCamera, pass, false );
				}
				
				glEnable( GL_BLEND );
				
				if( pass == render_main )
				{	
					if( HasFlag( gEngine->renderFlags, render_collisionTrees ) )
					{
						CCOctreeRender( collisionManager->tree );
					}
				}
				
				if( pass == render_main )
				{
					// Keep it disabled for the rest of the renders
					glDisable( GL_DEPTH_TEST );
				}
				
				for( int i=0; i<scenes.length; ++i )
				{
					scenes.list[i]->render( currentCamera, pass, true );
				}
				
				// Render all the visible collideables
				{

#if defined PROFILEON
					CCProfiler profile( "CCOctreeRenderVisibleObjects( true )" );
#endif
					CCOctreeRenderVisibleObjects( currentCamera, pass, true );
				}
			}
		}
		gUseProjectionMatrix = false;
		
		// 2D Rendering
		{
			CCMatrix matrix;
			CCMatrixLoadIdentity( matrix );
			CCMatrixRotate( matrix, renderer->orientation.current, 0.0f, 0.0f, 1.0f );
			CCMatrixOrtho( matrix, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f );
			GLSetPushMatrix( matrix );
			
            for( int i=0; i<scenes.length; ++i )
            {
                scenes.list[i]->render2DForeground( cameraIndex );
            }
			
#ifdef DEBUGON
			if( HasFlag( gEngine->renderFlags, render_fontPage ) )
			{
				CCSetColour( CCColour() );
				textureManager->fontPages.list[0]->view();
			}
#endif
		}
		
		currentCamera = NULL;
	}
}


void CCEngine::finishJobs()
{	
#if defined PROFILEON
    CCProfiler profile( "CCEngine::finishJobs()" );
#endif
    
    CCFileManager::readyIO();
    
    urlManager->updateEngineThread();
    
	// Prune the octree
	if( collisionManager->pruneTrees > 0.0f )
	{
		collisionManager->pruneTrees -= gameTime.real;
		if( collisionManager->pruneTrees <= 0.0f )
		{
            //DEBUGLOG( "Octree - prune" );
			CCOctreePruneTree( collisionManager->tree );
		}
	}
}


void CCEngine::restart()
{
    urlManager->flushPendingRequests();
    for( int i=scenes.length-1; i>=0; --i )
    {
        removeScene( scenes.list[i] );
    }
    
    start();
}


void CCEngine::addCollideable(CCSceneCollideable* collideable)
{
	collisionManager->objects[collisionManager->length++] = collideable;
	CCOctreeAddObject( collisionManager->tree, collideable );
}


void CCEngine::removeCollideable(CCSceneCollideable* collideable)
{	
	RemoveFromList( collideable, (void**)collisionManager->objects, &collisionManager->length );
	CCOctreeRemoveObject( collideable );
}


const bool CCEngine::serialize(const bool saving)
{
	return false;
}


void CCEngine::runOnNativeThread(CCLambdaCallback *lambdaCallback)
{
    CCEngineThreadLock();
    nativeThreadCallbacks.add( lambdaCallback );
    CCEngineThreadUnlock();
}


void CCEngine::runOnEngineThread(CCLambdaCallback *lambdaCallback)
{
    CCEngineThreadLock();
    engineThreadCallbacks.add( lambdaCallback );
    CCEngineThreadUnlock();
}
