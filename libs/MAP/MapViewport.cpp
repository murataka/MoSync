/* Copyright (C) 2010 Mobile Sorcery AB

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2, as published by
the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
*/

#include <matime.h>
#include "MemoryMgr.h"
#include <mastdlib.h>
#include <MAUtil/Graphics.h>
#include "MapViewport.h"
#include "MapCache.h"
#include "MapSource.h"
#include "DebugPrintf.h"

namespace MAP
{
	//
	// Appearance
	//
	static const bool ShowPixelScale = true; // shows scale slider
	static const bool ShowPixelScaleAsText = true; // shows meters/pixel scale (at latitude of screen center).
	static const bool ShowHairlineCross = true;
	static const bool ShowLatLon = true;
	//
	// Configuration
	//

	//
	// Pan smoothing time = PanInterval * PanAveragePoints
	//
	static const int PanInterval = 30;
	static const int PanAveragePoints = 8;
	static const double Tension = 4.0;
	static const double PanFriction = 0.60;
	static const double GlideFriction = 0.01;
	static const int SmallScrollStep = 30; // pixels to scroll if not full page
	static const int CrossSize = 4;

	//=========================================================================
	enum MapViewportMomentumState
	//=========================================================================
	{
		MapViewportMomentumState_None,
		MapViewportMomentumState_Initializing,
		MapViewportMomentumstate_Initialized,
		MapViewportMomentumState_Gliding
	};

	//=========================================================================
	class MapViewportIdleListener : public IdleListener
	//=========================================================================
	{
	public:
		//---------------------------------------------------------------------
		MapViewportIdleListener( MapViewport* viewport ) :
		//---------------------------------------------------------------------
			mMomentumX( 0 ),
			mMomentumY( 0 ),
			mViewport( viewport ),
			mPanTime( 0 ),
			points( 0 ),
			pointPtr( 0 )
		{
		}

		//---------------------------------------------------------------------
		void idle( )
		//---------------------------------------------------------------------
		{
			int currentTime = maGetMilliSecondCount( );
			int delta = currentTime - mPanTime;
			if ( delta < PanInterval )
				return;
			mPanTime = currentTime;

			PixelCoordinate prevCenterPix = mViewport->mCenterPositionPixels;
			PixelCoordinate targetPix = mViewport->mPanTargetPositionPixels;

			if ( mGliding )
			{
				//
				// Reduce momentum by friction
				//
				int time = currentTime - mGlideStartTime;
				double frictionFactor = pow( 1.0 - GlideFriction, 1.0 + (double)time / 1000 );
				mMomentumX = mMomentumX * frictionFactor;
				mMomentumY = mMomentumY * frictionFactor;

				// pow( 1 - friction, 1 + delta )
				//
				// calc new location based on momentum
				//
				mViewport->mCenterPositionPixels = PixelCoordinate(	mViewport->getMagnification( ),
																	(int)( mViewport->mCenterPositionPixels.getX( ) + 0.001 * mMomentumX * delta ),
																	(int)( mViewport->mCenterPositionPixels.getY( ) + 0.001 * mMomentumY * delta ) );
				//mViewport->mCenterPositionPixels = PixelCoordinate(	mViewport->getMagnification( ),
				//													(int)( mViewport->mCenterPositionPixels.getX( ) + 0.000001 * mMomentumX * time * time ),
				//													(int)( mViewport->mCenterPositionPixels.getY( ) + 0.000001 * mMomentumY * time * time ) );

				mViewport->mCenterPositionLonLat = LonLat( mViewport->mCenterPositionPixels );

				mViewport->mPanTargetPositionPixels = mViewport->mCenterPositionPixels;
				mViewport->mPanTargetPositionLonLat = mViewport->mCenterPositionLonLat;

				//DebugPrintf( "setCenterPosition: offset=%f %f, moment=%f %f\n", 
				//	//mViewport->mPanTargetPositionPixels.getX( ), 
				//	//mViewport->mPanTargetPositionPixels.getY( ), 
				//	offsetX,
				//	offsetY,
				//	mMomentumX, 
				//	mMomentumY );

				if ( fabs( mMomentumX ) < 1 && fabs( mMomentumY ) < 1 )
				{
					//
					// Done panning, stop timer and repaint
					//
					Environment::getEnvironment( ).removeIdleListener( this );
					mViewport->mHasTimer = false;
					mViewport->mCenterPositionPixels = mViewport->mPanTargetPositionPixels;
					mViewport->mCenterPositionLonLat = mViewport->mPanTargetPositionLonLat;
					//DebugPrintf( "At target: %d\n", currentTime );
				}

			}
			else
			{
				//
				// Add to moving average
				//
				PixelCoordinate pxy = mViewport->mPanTargetPositionPixels;
				px[pointPtr] = pxy.getX( );
				py[pointPtr] = pxy.getY( );
				pointPtr++;
				if ( pointPtr >= PanAveragePoints )
					pointPtr = 0;
				if ( points < PanAveragePoints )
					points++;
				//
				// Calc average
				//
				int sumx = 0;
				int sumy = 0;
				for ( int i = 0; i < points; i++ )
				{
					sumx += px[i];
					sumy += py[i];
				}
				int x = sumx / points;
				int y = sumy / points;
				PixelCoordinate newXy = PixelCoordinate( mViewport->getMagnification( ), x, y );

				mViewport->mCenterPositionPixels = newXy;
				mViewport->mCenterPositionLonLat = LonLat( mViewport->mCenterPositionPixels );

				mMomentumX = ( newXy.getX( ) - prevCenterPix.getX( ) ) * 1000 / delta;
				mMomentumY = ( newXy.getY( ) - prevCenterPix.getY( ) ) * 1000 / delta;

				//DebugPrintf( "Momentum: %f, %f\n", mMomentumX, mMomentumY );
				//
				// Stop panning if offset is small and no momentum
				//
				int offsetX = pxy.getX( ) - newXy.getX( );
				int offsetY = pxy.getY( ) - newXy.getY( );
				if ( abs( offsetX ) == 0 && abs( offsetY ) == 0 && fabs( mMomentumX ) < 1 && fabs( mMomentumY ) < 1 )
				{
					//
					// Done panning, stop timer and repaint
					//
					Environment::getEnvironment( ).removeIdleListener( this );
					mViewport->mHasTimer = false;
					mViewport->mCenterPositionPixels = mViewport->mPanTargetPositionPixels;
					mViewport->mCenterPositionLonLat = mViewport->mPanTargetPositionLonLat;
					//DebugPrintf( "At target: %d\n", currentTime );
				}

			}

			mViewport->updateMap( );
		}

		//---------------------------------------------------------------------
		void startGlide( )
		//---------------------------------------------------------------------
		{
			mGliding = true;
			mGlideStartTime = maGetMilliSecondCount( );
		}

		//---------------------------------------------------------------------
		void stopGlide( )
		//---------------------------------------------------------------------
		{
			mGliding = false;
			points = 0;
			pointPtr = 0;
		}


	private:
		MapViewport* mViewport;
		int mPanTime;
		int mGlideStartTime;
		bool mGliding;
		double mMomentumX;
		double mMomentumY;

		int px[PanAveragePoints];
		int py[PanAveragePoints];
		int points;
		int pointPtr;

	};

	//=========================================================================

	//-------------------------------------------------------------------------
	MapViewport::MapViewport( )
	//-------------------------------------------------------------------------
	:	mCenterPositionLonLat( ),
		mCenterPositionPixels( ),
		mPanTargetPositionLonLat( ),
		mPanTargetPositionPixels( ),
		mMagnification( 0 ),
		mSource( NULL ),
		mHasScale( true ),
		mIdleListener( NULL ),
		mFont( NULL ),
		mInDraw( NULL ),
		mScale(1.0)
	{
		mIdleListener = newobject( MapViewportIdleListener, new MapViewportIdleListener( this ) );
		Environment::getEnvironment( ).addIdleListener( mIdleListener );
	}

	//-------------------------------------------------------------------------
	MapViewport::~MapViewport( )
	//-------------------------------------------------------------------------
	{
		if ( mHasTimer )
			Environment::getEnvironment( ).removeIdleListener( mIdleListener );
		deleteobject( mIdleListener );
	}

	//-------------------------------------------------------------------------
	void MapViewport::setMapSource( MapSource* source )
	//-------------------------------------------------------------------------
	{
		mSource = source;
		updateMap( );
	}

	//-------------------------------------------------------------------------
	PixelCoordinate MapViewport::getCenterPositionPixels( ) const
	//-------------------------------------------------------------------------
	{
		return mPanTargetPositionPixels;
	}

	//-------------------------------------------------------------------------
	inline double Max( double x, double y )
	//-------------------------------------------------------------------------
	{
		return x > y ? x : y;
	}

	//-------------------------------------------------------------------------
	LonLat MapViewport::getCenterPosition( ) const
	//-------------------------------------------------------------------------
	{
		return mPanTargetPositionLonLat;
	}

	//-------------------------------------------------------------------------
	void MapViewport::setCenterPosition( LonLat position, int magnification, bool immediate, bool isPointerEvent )
	//-------------------------------------------------------------------------
	{
		int width = getWidth( );
		int height = getHeight( );

		if ( immediate || width <= 0 || height <= 0 )
		{
			mMagnification = magnification;
			mCenterPositionLonLat = mPanTargetPositionLonLat = position;
			mCenterPositionPixels = mPanTargetPositionPixels = position.toPixels( magnification );
			mIdleListener->stopGlide( );

			return;
		}

		PixelCoordinate prevTarget = mPanTargetPositionPixels;

		PixelCoordinate newXy = position.toPixels( magnification );
		//
		// Make sure current position is nearby, so we only soft scroll less than one screen.
		//
		int deltaX = newXy.getX( ) - mCenterPositionPixels.getX( );
		int deltaY = newXy.getY( ) - mCenterPositionPixels.getY( );

		//DebugPrintf( "Points: %d Ptr: %d Delta: %d %d\n", points, pointPtr, deltaX, deltaY );

		double factor = /* 6 * */ fabs( Max( (double)deltaX / width, (double)deltaY / height ) );
		if ( factor > 1 )
		{
			//
			// go directly to location if delta is more than 1/6 of viewport size.
			//
			newXy = PixelCoordinate(	magnification,
										mPanTargetPositionPixels.getX( ) - (int)( (double)deltaX / factor ),
										mPanTargetPositionPixels.getY( ) - (int)( (double)deltaY / factor ) );
		}
		if ( !isPointerEvent )
			mIdleListener->stopGlide( );

		mPanTargetPositionPixels = newXy;
		mPanTargetPositionLonLat = LonLat( newXy );
		mMagnification = magnification;

		if ( !mHasTimer )
		{
			Environment::getEnvironment( ).addIdleListener( mIdleListener );
			mHasTimer = true;
		}
	}

	//-------------------------------------------------------------------------
	void MapViewport::setCenterPosition( LonLat position, bool immediate, bool isPointerEvent )
	//-------------------------------------------------------------------------
	{
		setCenterPosition( position, mMagnification, immediate, isPointerEvent );
	}

	//-------------------------------------------------------------------------
	void MapViewport::startGlide( )
	//-------------------------------------------------------------------------
	{
		mIdleListener->startGlide( );
	}

	//-------------------------------------------------------------------------
	void MapViewport::stopGlide( )
	//-------------------------------------------------------------------------
	{
		mIdleListener->stopGlide( );
		//
		// make sure we lock map in place
		//
		mPanTargetPositionPixels = mCenterPositionPixels;
		updateMap( );
	}

	//-------------------------------------------------------------------------
	int MapViewport::getMagnification( ) const
	//-------------------------------------------------------------------------
	{
		return mMagnification;
	}

	//-------------------------------------------------------------------------
	void MapViewport::setMagnification( int magnification )
	//-------------------------------------------------------------------------
	{
		mMagnification = magnification;
		mIdleListener->stopGlide( );

		setCenterPosition( mPanTargetPositionLonLat, true, false );
	}

	//-------------------------------------------------------------------------
	void MapViewport::tileReceived( MapCache* sender, MapTile* tile )
	//-------------------------------------------------------------------------
	{
		if ( mInDraw )
		{
			//
			// draw context is set up, draw tile to viewport
			//
			PixelCoordinate tilePx = tile->getCenter( ).toPixels( tile->getMagnification( ) );
			MAPoint2d pt = worldPixelToViewport( tilePx );
			const int tileSize = mSource->getTileSize( );
			
#ifdef GFXOPENGL
			Gfx_pushMatrix();
			Gfx_scale((MAFixed)(mScale*65536.0), (MAFixed)(mScale*65536.0));

#endif

			Gfx_drawImage( tile->getImage( ),  pt.x - tileSize / 2, pt.y - tileSize / 2 );

#ifdef GFXOPENGL
			Gfx_popMatrix();
#endif
		}
		else
		{
			// added by niklas
			// TODO: Gfx_notifyImageUpdated( tile->getImage( ) );
			
			//
			// notify client that update is needed
			//
			onViewportUpdated( );
		}
	}

	//-------------------------------------------------------------------------
	void MapViewport::onViewportUpdated( )
	//-------------------------------------------------------------------------
	{
		Vector<IMapViewportListener*>* listeners = getBroadcasterListeners<IMapViewportListener>( *this );
		for ( int i = 0; i < listeners->size( ); i ++ )
			(*listeners)[i]->viewportUpdated( this );
	}

	//-------------------------------------------------------------------------
	void MapViewport::drawViewport( Point origin )
	//-------------------------------------------------------------------------
	{
		mInDraw = true;
		//
		// Save clip
		//
		(void)Gfx_pushClipRect( origin.x, origin.y, getWidth( ), getHeight( ) );
		Rect bounds = Rect( origin.x, origin.y, getWidth( ), getHeight( )  );
		//
		// Draw available tiles
		//
		MapCache::get( )->requestTiles( this, mSource, LonLat( mCenterPositionPixels ), mMagnification, getWidth( ), getHeight( ) );
		//
		// Let subclass draw its overlay
		//
		drawOverlay( bounds, mMagnification );
		
		//
		// Draw scale indicator
		//
		if ( ShowPixelScale && mHasScale )
		{
			const int scaleWidth = 80;
			const int scaleX = origin.x + getWidth( ) - scaleWidth - 5;
			const int scaleY = origin.y + 5;
			int lineThickness = 3;
			const int crossbarHeight = 7;
			float scaleFrac = (float)( mMagnification - mSource->getMagnificationMin( ) ) / ( mSource->getMagnificationMax( ) - mSource->getMagnificationMin( ) );

			maSetColor( 0xa0a0a0 );

			Gfx_fillRect( scaleX, scaleY - lineThickness / 2, scaleWidth, lineThickness );
			Gfx_fillRect( (int)( scaleX + scaleFrac * scaleWidth - 0.5f * lineThickness ), scaleY - (crossbarHeight / 2 ), lineThickness, crossbarHeight );

			lineThickness = 1;

			maSetColor( 0x000000 );

			Gfx_fillRect( scaleX, scaleY, scaleWidth, lineThickness );
			Gfx_fillRect( (int)( scaleX + scaleFrac * scaleWidth - 0.5f * lineThickness ), scaleY - (crossbarHeight / 2 ), lineThickness, crossbarHeight );

			//
			// pixel scale
			//
			if ( ShowPixelScaleAsText )
			{
				int mag = getMagnification( );
				PixelCoordinate px1 = getCenterPositionPixels( );
				LonLat p1 = LonLat( px1 );
				PixelCoordinate px2 = PixelCoordinate( mag, px1.getX( ) + 1, px1.getY( ) + 1 );
				LonLat p2 = LonLat( px2 );
				double meterX1, meterY1;
				double meterX2, meterY2;
				p1.toMeters( meterX1, meterY1 );
				p2.toMeters( meterX2, meterY2 );
				double offsetX = meterX2 - meterX1;
				offsetX *= cos( fabs( p2.lat * PI / 180 ) );
				char buffer[100];
				sprintf( buffer, "%5.2f m/px", offsetX );

				if ( mFont != NULL )
					mFont->drawString( buffer, scaleX, scaleY + 5 );
				else
					Gfx_drawText( scaleX, scaleY + 5, buffer );
			}
		}
		//
		// Draw hairline cross
		//
		if ( ShowHairlineCross )
		{
			const int centerX = origin.x + getWidth( ) / 2;
			const int centerY = origin.y + getHeight( ) / 2;

			maSetColor( 0x000000 );

			Gfx_fillRect( centerX, centerY - CrossSize, 1, CrossSize * 2 + 1 );
			Gfx_fillRect( centerX - CrossSize, centerY, CrossSize * 2 + 1, 1 );
		}
		//
		// Draw latitude, longitude
		//
		if ( ShowLatLon )
		{
			char buffer[100];
			sprintf( buffer, "%-3.4f %-3.4f", mCenterPositionLonLat.lon, mCenterPositionLonLat.lat );

			maSetColor( 0x000000 );

			if ( mFont != NULL )
				mFont->drawString( buffer, origin.x, origin.y );
			else
				Gfx_drawText( origin.x, origin.y, buffer );
		}
		//
		// Draw debug info
		//
		if ( ShowLatLon )
		{
			char buffer[100];
			sprintf( buffer, "Tiles: %d Cache: %d", this->mSource->getTileCount( ), MapCache::get( )->size( ) );
			maSetColor( 0x000000 );

			if ( mFont != NULL )
				mFont->drawString( buffer, origin.x, origin.y + 20 );
			else
				Gfx_drawText( origin.x, origin.y + 20, buffer );
		}
		//
		// Restore original clip
		//
		(void)Gfx_popClipRect( );

		mInDraw = false;
	}

	//-------------------------------------------------------------------------
	void MapViewport::drawOverlay( Rect& bounds, int magnification )
	//-------------------------------------------------------------------------
	{
	}

	//-------------------------------------------------------------------------
	void MapViewport::updateMap( )
	//-------------------------------------------------------------------------
	{
		if ( getWidth( ) <= 0 || getHeight( ) <= 0 ) 
			return;
		//
		// Request tiles
		//
		// We want to use currently displayed center position here, so we bypass getCenterPosition( ).
		//
		MapCache::get( )->requestTiles( this, mSource, LonLat( mCenterPositionPixels ), mMagnification, getWidth( ), getHeight( ) );
	}

	//-------------------------------------------------------------------------
	MAPoint2d MapViewport::worldPixelToViewport( PixelCoordinate wpx )
	//-------------------------------------------------------------------------
	{
		MAPoint2d pt;
		PixelCoordinate screenPx = mCenterPositionPixels;

		pt.x =    wpx.getX( ) - screenPx.getX( )   + ( getWidth( ) >> 1 );
		pt.y = -( wpx.getY( ) - screenPx.getY( ) ) + ( getHeight( ) >> 1 );

		return pt;
	}

	//-------------------------------------------------------------------------
	PixelCoordinate MapViewport::viewportToWorldPixel( MAPoint2d pt )
	//-------------------------------------------------------------------------
	{
		PixelCoordinate screenPx = mCenterPositionPixels;
		return PixelCoordinate( mMagnification,
								(int)( pt.x + 0.5 - 0.5 * getWidth( ) + screenPx.getX( ) ),
								(int)( -( pt.y + 0.5 - 0.5 * getHeight( ) - screenPx.getY( ) ) ) );
	}

	//-------------------------------------------------------------------------
	static double clamp( double x, double min, double max )
	//-------------------------------------------------------------------------
	{
		return x < min ? min : x > max ? max : x;
	}

	//-------------------------------------------------------------------------
	void MapViewport::zoomIn( )
	//-------------------------------------------------------------------------
	{
		if ( mMagnification < mSource->getMagnificationMax( ) )
		{
			mMagnification++;
			mIdleListener->stopGlide( );

			mCenterPositionPixels = mCenterPositionLonLat.toPixels( mMagnification );
			mPanTargetPositionPixels = mPanTargetPositionLonLat.toPixels( mMagnification );
		}
	}

	//-------------------------------------------------------------------------
	void MapViewport::zoomOut( )
	//-------------------------------------------------------------------------
	{
		if ( mMagnification > mSource->getMagnificationMin( ) )
		{
			mMagnification--;
			mIdleListener->stopGlide( );
			mCenterPositionPixels = mCenterPositionLonLat.toPixels( mMagnification );
			mPanTargetPositionPixels = mPanTargetPositionLonLat.toPixels( mMagnification );
		}
	}

	//-------------------------------------------------------------------------
	void MapViewport::scroll( MapViewportScrollDirection direction, bool largeStep )
	//-------------------------------------------------------------------------
	{
		PixelCoordinate px = getCenterPositionPixels( );
		const int hStep = largeStep ? getWidth( ) : SmallScrollStep;
		const int vStep = largeStep ? getHeight( ) : SmallScrollStep;

		switch( direction )
		{
		case SCROLLDIRECTION_NORTH: px = PixelCoordinate( px.getMagnification( ), px.getX( ), px.getY( ) + vStep ); break;
		case SCROLLDIRECTION_SOUTH: px = PixelCoordinate( px.getMagnification( ), px.getX( ), px.getY( ) - vStep ); break;
		case SCROLLDIRECTION_EAST:  px = PixelCoordinate( px.getMagnification( ), px.getX( ) + hStep, px.getY( ) ); break;
		case SCROLLDIRECTION_WEST:  px = PixelCoordinate( px.getMagnification( ), px.getX( ) - hStep, px.getY( ) ); break;
		}

		LonLat newPos = LonLat( px );
		newPos = LonLat( clamp( newPos.lon, -180, 180 ), clamp( newPos.lat, -85, 85 ) );
		setCenterPosition( newPos, false, false );
	}

	//-------------------------------------------------------------------------
	bool MapViewport::handleKeyPress( int keyCode )
	//-------------------------------------------------------------------------
	{
		bool ret = false;
		switch( keyCode )
		{
		case MAK_LEFT:		
			scroll( SCROLLDIRECTION_WEST, false ); 
			ret = true;
			break;
		case MAK_RIGHT:		
			scroll( SCROLLDIRECTION_EAST, false ); 
			ret = true;
			break;
		case MAK_UP:		
			scroll( SCROLLDIRECTION_NORTH, false ); 
			ret = true;
			break;
		case MAK_DOWN:		
			scroll( SCROLLDIRECTION_SOUTH, false ); 
			ret = true;
			break;
		case MAK_1:			
			zoomOut( ); 
			ret = true;
			break;
		case MAK_3:			
			zoomIn( ); 
			ret = true;
			break;
		default: 
			ret = false;
			break;
		}
		this->updateMap( );
		return ret;
	}

	//-------------------------------------------------------------------------
	bool MapViewport::handleKeyRelease( int keyCode )
	//-------------------------------------------------------------------------
	{
		return true;
	}

	//-------------------------------------------------------------------------
	void MapViewport::setWidth( int width )
	//-------------------------------------------------------------------------
	{
		mWidth = width;
		updateMap( );
	}

	//-------------------------------------------------------------------------
	void MapViewport::setHeight( int height )
	//-------------------------------------------------------------------------
	{
		mHeight = height;
		updateMap( );
	}
}
