#include "tools.h"
#include "tmath.h"
#include "defines.h"
#include "Modules.h"
#include "Graphics.h"
#include "Polygon.h"

namespace t3d {

void DrawGouraudTriangleAlpha32(PolygonF* face, unsigned char* _dest_buffer, int mem_pitch, int alpha)
{
	// this function draws a gouraud shaded polygon, based on the affine texture mapper, instead
	// of interpolating the texture coordinates, we simply interpolate the (R,G,B) values across
	// the polygons, I simply needed at another interpolant, I have mapped u->red, v->green, w->blue

	int v0=0,
		v1=1,
		v2=2,
		tri_type = TRI_TYPE_NONE,
		irestart = INTERP_LHS;

	int dx,dy,dyl,dyr,      // general deltas
		u,v,w,
		du,dv,dw,
		xi,yi,              // the current interpolated x,y
		ui,vi,wi,           // the current interpolated u,v
		index_x,index_y,    // looping vars
		x,y,                // hold general x,y
		xstart,
		xend,
		ystart,
		yrestart,
		yend,
		xl,                 
		dxdyl,              
		xr,
		dxdyr,             
		dudyl,    
		ul,
		dvdyl,   
		vl,
		dwdyl,   
		wl,
		dudyr,
		ur,
		dvdyr,
		vr,
		dwdyr,
		wr;

	int x0,y0,tu0,tv0,tw0,    // cached vertices
		x1,y1,tu1,tv1,tw1,
		x2,y2,tu2,tv2,tw2;

	int r_base0, g_base0, b_base0,
		r_base1, g_base1, b_base1,
		r_base2, g_base2, b_base2;

	unsigned int *screen_ptr  = NULL,
				 *screen_line = NULL,
				 *textmap     = NULL,
				 *dest_buffer = (unsigned int*)_dest_buffer;

#ifdef DEBUG_ON
	// track rendering stats
	debug_polys_rendered_per_frame++;
#endif

	// adjust memory pitch to words, divide by 4
	mem_pitch >>= 2;

	// apply fill convention to coordinates
	face->tvlist[0].x = (int)(face->tvlist[0].x+0.0);
	face->tvlist[0].y = (int)(face->tvlist[0].y+0.0);

	face->tvlist[1].x = (int)(face->tvlist[1].x+0.0);
	face->tvlist[1].y = (int)(face->tvlist[1].y+0.0);

	face->tvlist[2].x = (int)(face->tvlist[2].x+0.0);
	face->tvlist[2].y = (int)(face->tvlist[2].y+0.0);

	int min_clip_x;
	int max_clip_x;
	int min_clip_y;
	int max_clip_y;
	Modules::GetGraphics().GetClipValue(min_clip_x,
		max_clip_x, min_clip_y, max_clip_y);

	// first trivial clipping rejection tests 
	if (((face->tvlist[0].y < min_clip_y)  && 
		(face->tvlist[1].y < min_clip_y)  &&
		(face->tvlist[2].y < min_clip_y)) ||

		((face->tvlist[0].y > max_clip_y)  && 
		(face->tvlist[1].y > max_clip_y)  &&
		(face->tvlist[2].y > max_clip_y)) ||

		((face->tvlist[0].x < min_clip_x)  && 
		(face->tvlist[1].x < min_clip_x)  &&
		(face->tvlist[2].x < min_clip_x)) ||

		((face->tvlist[0].x > max_clip_x)  && 
		(face->tvlist[1].x > max_clip_x)  &&
		(face->tvlist[2].x > max_clip_x)))
		return;

	// sort vertices
	if (face->tvlist[v1].y < face->tvlist[v0].y) 
		std::swap(v0, v1);

	if (face->tvlist[v2].y < face->tvlist[v0].y) 
		std::swap(v0, v2);

	if (face->tvlist[v2].y < face->tvlist[v1].y) 
		std::swap(v1, v2);

	// now test for trivial flat sided cases
	if (FCMP(face->tvlist[v0].y, face->tvlist[v1].y))
	{ 
		// set triangle type
		tri_type = TRI_TYPE_FLAT_TOP;

		// sort vertices left to right
		if (face->tvlist[v1].x < face->tvlist[v0].x) 
			std::swap(v0, v1);

	}
	// now test for trivial flat sided cases
	else if (FCMP(face->tvlist[v1].y, face->tvlist[v2].y))
	{ 
		// set triangle type
		tri_type = TRI_TYPE_FLAT_BOTTOM;

		// sort vertices left to right
		if (face->tvlist[v2].x < face->tvlist[v1].x) 
			std::swap(v1, v2);
	}
	else
	{
		// must be a general triangle
		tri_type = TRI_TYPE_GENERAL;
	}

	int tmpa;
	_RGB8888FROM32BIT(face->lit_color[v0], &tmpa, &r_base0, &g_base0, &b_base0);
	_RGB8888FROM32BIT(face->lit_color[v1], &tmpa, &r_base1, &g_base1, &b_base1);
	_RGB8888FROM32BIT(face->lit_color[v2], &tmpa, &r_base2, &g_base2, &b_base2);

	// extract vertices for processing, now that we have order
	x0  = (int)(face->tvlist[v0].x);
	y0  = (int)(face->tvlist[v0].y);

	tu0 = r_base0;
	tv0 = g_base0; 
	tw0 = b_base0; 

	x1  = (int)(face->tvlist[v1].x);
	y1  = (int)(face->tvlist[v1].y);

	tu1 = r_base1;
	tv1 = g_base1; 
	tw1 = b_base1; 

	x2  = (int)(face->tvlist[v2].x);
	y2  = (int)(face->tvlist[v2].y);

	tu2 = r_base2; 
	tv2 = g_base2; 
	tw2 = b_base2; 

	// degenerate triangle
	if ( ((x0 == x1) && (x1 == x2)) || ((y0 ==  y1) && (y1 == y2)))
		return;

	// set interpolation restart value
	yrestart = y1;

	// what kind of triangle
	if (tri_type & TRI_TYPE_FLAT_MASK)
	{
		if (tri_type == TRI_TYPE_FLAT_TOP)
		{
			// compute all deltas
			dy = (y2 - y0);

			dxdyl = ((x2 - x0)   << FIXP16_SHIFT)/dy;
			dudyl = ((tu2 - tu0) << FIXP16_SHIFT)/dy;  
			dvdyl = ((tv2 - tv0) << FIXP16_SHIFT)/dy;    
			dwdyl = ((tw2 - tw0) << FIXP16_SHIFT)/dy;  

			dxdyr = ((x2 - x1)   << FIXP16_SHIFT)/dy;
			dudyr = ((tu2 - tu1) << FIXP16_SHIFT)/dy;  
			dvdyr = ((tv2 - tv1) << FIXP16_SHIFT)/dy;   
			dwdyr = ((tw2 - tw1) << FIXP16_SHIFT)/dy;   

			// test for y clipping
			if (y0 < min_clip_y)
			{
				// compute overclip
				dy = (min_clip_y - y0);

				// computer new LHS starting values
				xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
				ul = dudyl*dy + (tu0 << FIXP16_SHIFT);
				vl = dvdyl*dy + (tv0 << FIXP16_SHIFT);
				wl = dwdyl*dy + (tw0 << FIXP16_SHIFT);

				// compute new RHS starting values
				xr = dxdyr*dy + (x1  << FIXP16_SHIFT);
				ur = dudyr*dy + (tu1 << FIXP16_SHIFT);
				vr = dvdyr*dy + (tv1 << FIXP16_SHIFT);
				wr = dwdyr*dy + (tw1 << FIXP16_SHIFT);

				// compute new starting y
				ystart = min_clip_y;

			} // end if
			else
			{
				// no clipping

				// set starting values
				xl = (x0 << FIXP16_SHIFT);
				xr = (x1 << FIXP16_SHIFT);

				ul = (tu0 << FIXP16_SHIFT);
				vl = (tv0 << FIXP16_SHIFT);
				wl = (tw0 << FIXP16_SHIFT);

				ur = (tu1 << FIXP16_SHIFT);
				vr = (tv1 << FIXP16_SHIFT);
				wr = (tw1 << FIXP16_SHIFT);

				// set starting y
				ystart = y0;

			}
		}
		else
		{
			// must be flat bottom

			// compute all deltas
			dy = (y1 - y0);

			dxdyl = ((x1 - x0)   << FIXP16_SHIFT)/dy;
			dudyl = ((tu1 - tu0) << FIXP16_SHIFT)/dy;  
			dvdyl = ((tv1 - tv0) << FIXP16_SHIFT)/dy;    
			dwdyl = ((tw1 - tw0) << FIXP16_SHIFT)/dy; 

			dxdyr = ((x2 - x0)   << FIXP16_SHIFT)/dy;
			dudyr = ((tu2 - tu0) << FIXP16_SHIFT)/dy;  
			dvdyr = ((tv2 - tv0) << FIXP16_SHIFT)/dy;   
			dwdyr = ((tw2 - tw0) << FIXP16_SHIFT)/dy;   

			// test for y clipping
			if (y0 < min_clip_y)
			{
				// compute overclip
				dy = (min_clip_y - y0);

				// computer new LHS starting values
				xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
				ul = dudyl*dy + (tu0 << FIXP16_SHIFT);
				vl = dvdyl*dy + (tv0 << FIXP16_SHIFT);
				wl = dwdyl*dy + (tw0 << FIXP16_SHIFT);

				// compute new RHS starting values
				xr = dxdyr*dy + (x0  << FIXP16_SHIFT);
				ur = dudyr*dy + (tu0 << FIXP16_SHIFT);
				vr = dvdyr*dy + (tv0 << FIXP16_SHIFT);
				wr = dwdyr*dy + (tw0 << FIXP16_SHIFT);

				// compute new starting y
				ystart = min_clip_y;

			} // end if
			else
			{
				// no clipping

				// set starting values
				xl = (x0 << FIXP16_SHIFT);
				xr = (x0 << FIXP16_SHIFT);

				ul = (tu0 << FIXP16_SHIFT);
				vl = (tv0 << FIXP16_SHIFT);
				wl = (tw0 << FIXP16_SHIFT);

				ur = (tu0 << FIXP16_SHIFT);
				vr = (tv0 << FIXP16_SHIFT);
				wr = (tw0 << FIXP16_SHIFT);

				// set starting y
				ystart = y0;
			}
		}

		// test for bottom clip, always
		if ((yend = y2) > max_clip_y)
			yend = max_clip_y;

		// test for horizontal clipping
		if ((x0 < min_clip_x) || (x0 > max_clip_x) ||
			(x1 < min_clip_x) || (x1 > max_clip_x) ||
			(x2 < min_clip_x) || (x2 > max_clip_x))
		{
			// clip version

			// point screen ptr to starting line
			screen_ptr = dest_buffer + (ystart * mem_pitch);

			for (yi = ystart; yi < yend; yi++)
			{
				// compute span endpoints
				xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
				xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

				// compute starting points for u,v,w interpolants
				ui = ul + FIXP16_ROUND_UP;
				vi = vl + FIXP16_ROUND_UP;
				wi = wl + FIXP16_ROUND_UP;

				// compute u,v interpolants
				if ((dx = (xend - xstart))>0)
				{
					du = (ur - ul)/dx;
					dv = (vr - vl)/dx;
					dw = (wr - wl)/dx;
				} // end if
				else
				{
					du = (ur - ul);
					dv = (vr - vl);
					dw = (wr - wl);
				} // end else

				///////////////////////////////////////////////////////////////////////

				// test for x clipping, LHS
				if (xstart < min_clip_x)
				{
					// compute x overlap
					dx = min_clip_x - xstart;

					// slide interpolants over
					ui+=dx*du;
					vi+=dx*dv;
					wi+=dx*dw;

					// reset vars
					xstart = min_clip_x;

				} // end if

				// test for x clipping RHS
				if (xend > max_clip_x)
					xend = max_clip_x;

				///////////////////////////////////////////////////////////////////////

				// draw span
				for (xi=xstart; xi < xend; xi++)
				{
					// write textel assume 8.8.8
					int tmpa;
					int r0 = ui >> FIXP16_SHIFT, 
						g0 = vi >> FIXP16_SHIFT, 
						b0 = wi >> FIXP16_SHIFT;
					int r1, g1, b1;
					_RGB8888FROM32BIT(screen_ptr[xi], &tmpa, &r1, &g1, &b1);
					r0 = r0 * alpha + r1 * (255 - alpha);
					g0 = g0 * alpha + g1 * (255 - alpha);
					b0 = b0 * alpha + b1 * (255 - alpha);
					screen_ptr[xi] = _RGB32BIT(255, r0 >> 8, g0 >> 8, b0 >> 8);

					// interpolate u,v
					ui+=du;
					vi+=dv;
					wi+=dw;
				} // end for xi

				// interpolate u,v,w,x along right and left edge
				xl+=dxdyl;
				ul+=dudyl;
				vl+=dvdyl;
				wl+=dwdyl;

				xr+=dxdyr;
				ur+=dudyr;
				vr+=dvdyr;
				wr+=dwdyr;

				// advance screen ptr
				screen_ptr+=mem_pitch;
			}
		}
		else
		{
			// non-clip version

			// point screen ptr to starting line
			screen_ptr = dest_buffer + (ystart * mem_pitch);

			for (yi = ystart; yi < yend; yi++)
			{
				// compute span endpoints
				xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
				xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

				// compute starting points for u,v,w interpolants
				ui = ul + FIXP16_ROUND_UP;
				vi = vl + FIXP16_ROUND_UP;
				wi = wl + FIXP16_ROUND_UP;

				// compute u,v interpolants
				if ((dx = (xend - xstart))>0)
				{
					du = (ur - ul)/dx;
					dv = (vr - vl)/dx;
					dw = (wr - wl)/dx;
				} // end if
				else
				{
					du = (ur - ul);
					dv = (vr - vl);
					dw = (wr - wl);
				} // end else

				// draw span
				for (xi=xstart; xi < xend; xi++)
				{
					// write textel assume 8.8.8
					int tmpa;
					int r0 = ui >> FIXP16_SHIFT, 
						g0 = vi >> FIXP16_SHIFT, 
						b0 = wi >> FIXP16_SHIFT;
					int r1, g1, b1;
					_RGB8888FROM32BIT(screen_ptr[xi], &tmpa, &r1, &g1, &b1);
					r0 = r0 * alpha + r1 * (255 - alpha);
					g0 = g0 * alpha + g1 * (255 - alpha);
					b0 = b0 * alpha + b1 * (255 - alpha);
					screen_ptr[xi] = _RGB32BIT(255, r0 >> 8, g0 >> 8, b0 >> 8);

					// interpolate u,v
					ui+=du;
					vi+=dv;
					wi+=dw;
				} // end for xi

				// interpolate u,v,w,x along right and left edge
				xl+=dxdyl;
				ul+=dudyl;
				vl+=dvdyl;
				wl+=dwdyl;

				xr+=dxdyr;
				ur+=dudyr;
				vr+=dvdyr;
				wr+=dwdyr;

				// advance screen ptr
				screen_ptr+=mem_pitch;
			}
		}
	}
	else if (tri_type==TRI_TYPE_GENERAL)
	{

		// first test for bottom clip, always
		if ((yend = y2) > max_clip_y)
			yend = max_clip_y;

		// pre-test y clipping status
		if (y1 < min_clip_y)
		{
			// compute all deltas
			// LHS
			dyl = (y2 - y1);

			dxdyl = ((x2  - x1)  << FIXP16_SHIFT)/dyl;
			dudyl = ((tu2 - tu1) << FIXP16_SHIFT)/dyl;  
			dvdyl = ((tv2 - tv1) << FIXP16_SHIFT)/dyl;    
			dwdyl = ((tw2 - tw1) << FIXP16_SHIFT)/dyl;  

			// RHS
			dyr = (y2 - y0);	

			dxdyr = ((x2  - x0)  << FIXP16_SHIFT)/dyr;
			dudyr = ((tu2 - tu0) << FIXP16_SHIFT)/dyr;  
			dvdyr = ((tv2 - tv0) << FIXP16_SHIFT)/dyr;   
			dwdyr = ((tw2 - tw0) << FIXP16_SHIFT)/dyr;   

			// compute overclip
			dyr = (min_clip_y - y0);
			dyl = (min_clip_y - y1);

			// computer new LHS starting values
			xl = dxdyl*dyl + (x1  << FIXP16_SHIFT);

			ul = dudyl*dyl + (tu1 << FIXP16_SHIFT);
			vl = dvdyl*dyl + (tv1 << FIXP16_SHIFT);
			wl = dwdyl*dyl + (tw1 << FIXP16_SHIFT);

			// compute new RHS starting values
			xr = dxdyr*dyr + (x0  << FIXP16_SHIFT);

			ur = dudyr*dyr + (tu0 << FIXP16_SHIFT);
			vr = dvdyr*dyr + (tv0 << FIXP16_SHIFT);
			wr = dwdyr*dyr + (tw0 << FIXP16_SHIFT);

			// compute new starting y
			ystart = min_clip_y;

			// test if we need swap to keep rendering left to right
			if (dxdyr > dxdyl)
			{
				std::swap(dxdyl,dxdyr);
				std::swap(dudyl,dudyr);
				std::swap(dvdyl,dvdyr);
				std::swap(dwdyl,dwdyr);
				std::swap(xl,xr);
				std::swap(ul,ur);
				std::swap(vl,vr);
				std::swap(wl,wr);
				std::swap(x1,x2);
				std::swap(y1,y2);
				std::swap(tu1,tu2);
				std::swap(tv1,tv2);
				std::swap(tw1,tw2);

				// set interpolation restart
				irestart = INTERP_RHS;
			}
		}
		else if (y0 < min_clip_y)
		{
			// compute all deltas
			// LHS
			dyl = (y1 - y0);

			dxdyl = ((x1  - x0)  << FIXP16_SHIFT)/dyl;
			dudyl = ((tu1 - tu0) << FIXP16_SHIFT)/dyl;  
			dvdyl = ((tv1 - tv0) << FIXP16_SHIFT)/dyl;    
			dwdyl = ((tw1 - tw0) << FIXP16_SHIFT)/dyl; 

			// RHS
			dyr = (y2 - y0);	

			dxdyr = ((x2  - x0)  << FIXP16_SHIFT)/dyr;
			dudyr = ((tu2 - tu0) << FIXP16_SHIFT)/dyr;  
			dvdyr = ((tv2 - tv0) << FIXP16_SHIFT)/dyr;   
			dwdyr = ((tw2 - tw0) << FIXP16_SHIFT)/dyr;   

			// compute overclip
			dy = (min_clip_y - y0);

			// computer new LHS starting values
			xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
			ul = dudyl*dy + (tu0 << FIXP16_SHIFT);
			vl = dvdyl*dy + (tv0 << FIXP16_SHIFT);
			wl = dwdyl*dy + (tw0 << FIXP16_SHIFT);

			// compute new RHS starting values
			xr = dxdyr*dy + (x0  << FIXP16_SHIFT);
			ur = dudyr*dy + (tu0 << FIXP16_SHIFT);
			vr = dvdyr*dy + (tv0 << FIXP16_SHIFT);
			wr = dwdyr*dy + (tw0 << FIXP16_SHIFT);

			// compute new starting y
			ystart = min_clip_y;

			// test if we need swap to keep rendering left to right
			if (dxdyr < dxdyl)
			{
				std::swap(dxdyl,dxdyr);
				std::swap(dudyl,dudyr);
				std::swap(dvdyl,dvdyr);
				std::swap(dwdyl,dwdyr);
				std::swap(xl,xr);
				std::swap(ul,ur);
				std::swap(vl,vr);
				std::swap(wl,wr);
				std::swap(x1,x2);
				std::swap(y1,y2);
				std::swap(tu1,tu2);
				std::swap(tv1,tv2);
				std::swap(tw1,tw2);

				// set interpolation restart
				irestart = INTERP_RHS;
			}
		}
		else
		{
			// no initial y clipping

			// compute all deltas
			// LHS
			dyl = (y1 - y0);

			dxdyl = ((x1  - x0)  << FIXP16_SHIFT)/dyl;
			dudyl = ((tu1 - tu0) << FIXP16_SHIFT)/dyl;  
			dvdyl = ((tv1 - tv0) << FIXP16_SHIFT)/dyl;    
			dwdyl = ((tw1 - tw0) << FIXP16_SHIFT)/dyl;   

			// RHS
			dyr = (y2 - y0);	

			dxdyr = ((x2 - x0)   << FIXP16_SHIFT)/dyr;
			dudyr = ((tu2 - tu0) << FIXP16_SHIFT)/dyr;  
			dvdyr = ((tv2 - tv0) << FIXP16_SHIFT)/dyr;   		
			dwdyr = ((tw2 - tw0) << FIXP16_SHIFT)/dyr;

			// no clipping y

			// set starting values
			xl = (x0 << FIXP16_SHIFT);
			xr = (x0 << FIXP16_SHIFT);

			ul = (tu0 << FIXP16_SHIFT);
			vl = (tv0 << FIXP16_SHIFT);
			wl = (tw0 << FIXP16_SHIFT);

			ur = (tu0 << FIXP16_SHIFT);
			vr = (tv0 << FIXP16_SHIFT);
			wr = (tw0 << FIXP16_SHIFT);

			// set starting y
			ystart = y0;

			// test if we need swap to keep rendering left to right
			if (dxdyr < dxdyl)
			{
				std::swap(dxdyl,dxdyr);
				std::swap(dudyl,dudyr);
				std::swap(dvdyl,dvdyr);
				std::swap(dwdyl,dwdyr);
				std::swap(xl,xr);
				std::swap(ul,ur);
				std::swap(vl,vr);
				std::swap(wl,wr);
				std::swap(x1,x2);
				std::swap(y1,y2);
				std::swap(tu1,tu2);
				std::swap(tv1,tv2);
				std::swap(tw1,tw2);

				// set interpolation restart
				irestart = INTERP_RHS;
			}
		}

		// test for horizontal clipping
		if ((x0 < min_clip_x) || (x0 > max_clip_x) ||
			(x1 < min_clip_x) || (x1 > max_clip_x) ||
			(x2 < min_clip_x) || (x2 > max_clip_x))
		{
			// clip version
			// x clipping	

			// point screen ptr to starting line
			screen_ptr = dest_buffer + (ystart * mem_pitch);

			for (yi = ystart; yi < yend; yi++)
			{
				// compute span endpoints
				xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
				xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

				// compute starting points for u,v,w interpolants
				ui = ul + FIXP16_ROUND_UP;
				vi = vl + FIXP16_ROUND_UP;
				wi = wl + FIXP16_ROUND_UP;

				// compute u,v interpolants
				if ((dx = (xend - xstart))>0)
				{
					du = (ur - ul)/dx;
					dv = (vr - vl)/dx;
					dw = (wr - wl)/dx;
				} // end if
				else
				{
					du = (ur - ul);
					dv = (vr - vl);
					dw = (wr - wl);
				} // end else

				///////////////////////////////////////////////////////////////////////

				// test for x clipping, LHS
				if (xstart < min_clip_x)
				{
					// compute x overlap
					dx = min_clip_x - xstart;

					// slide interpolants over
					ui+=dx*du;
					vi+=dx*dv;
					wi+=dx*dw;

					// set x to left clip edge
					xstart = min_clip_x;

				} // end if

				// test for x clipping RHS
				if (xend > max_clip_x)
					xend = max_clip_x;

				///////////////////////////////////////////////////////////////////////

				// draw span
				for (xi=xstart; xi < xend; xi++)
				{
					// write textel assume 8.8.8
					int tmpa;
					int r0 = ui >> FIXP16_SHIFT, 
						g0 = vi >> FIXP16_SHIFT, 
						b0 = wi >> FIXP16_SHIFT;
					int r1, g1, b1;
					_RGB8888FROM32BIT(screen_ptr[xi], &tmpa, &r1, &g1, &b1);
					r0 = r0 * alpha + r1 * (255 - alpha);
					g0 = g0 * alpha + g1 * (255 - alpha);
					b0 = b0 * alpha + b1 * (255 - alpha);
					screen_ptr[xi] = _RGB32BIT(255, r0 >> 8, g0 >> 8, b0 >> 8);

					// interpolate u,v
					ui+=du;
					vi+=dv;
					wi+=dw;
				} // end for xi

				// interpolate u,v,w,x along right and left edge
				xl+=dxdyl;
				ul+=dudyl;
				vl+=dvdyl;
				wl+=dwdyl;

				xr+=dxdyr;
				ur+=dudyr;
				vr+=dvdyr;
				wr+=dwdyr;

				// advance screen ptr
				screen_ptr+=mem_pitch;

				// test for yi hitting second region, if so change interpolant
				if (yi==yrestart)
				{
					// test interpolation side change flag

					if (irestart == INTERP_LHS)
					{
						// LHS
						dyl = (y2 - y1);	

						dxdyl = ((x2 - x1)   << FIXP16_SHIFT)/dyl;
						dudyl = ((tu2 - tu1) << FIXP16_SHIFT)/dyl;  
						dvdyl = ((tv2 - tv1) << FIXP16_SHIFT)/dyl;   		
						dwdyl = ((tw2 - tw1) << FIXP16_SHIFT)/dyl;  

						// set starting values
						xl = (x1  << FIXP16_SHIFT);
						ul = (tu1 << FIXP16_SHIFT);
						vl = (tv1 << FIXP16_SHIFT);
						wl = (tw1 << FIXP16_SHIFT);

						// interpolate down on LHS to even up
						xl+=dxdyl;
						ul+=dudyl;
						vl+=dvdyl;
						wl+=dwdyl;
					} // end if
					else
					{
						// RHS
						dyr = (y1 - y2);	

						dxdyr = ((x1 - x2)   << FIXP16_SHIFT)/dyr;
						dudyr = ((tu1 - tu2) << FIXP16_SHIFT)/dyr;  
						dvdyr = ((tv1 - tv2) << FIXP16_SHIFT)/dyr;   		
						dwdyr = ((tw1 - tw2) << FIXP16_SHIFT)/dyr;   		

						// set starting values
						xr = (x2  << FIXP16_SHIFT);
						ur = (tu2 << FIXP16_SHIFT);
						vr = (tv2 << FIXP16_SHIFT);
						wr = (tw2 << FIXP16_SHIFT);

						// interpolate down on RHS to even up
						xr+=dxdyr;
						ur+=dudyr;
						vr+=dvdyr;
						wr+=dwdyr;

					} // end else

				} // end if

			} // end for y

		} // end if
		else
		{
			// no x clipping
			// point screen ptr to starting line
			screen_ptr = dest_buffer + (ystart * mem_pitch);

			for (yi = ystart; yi < yend; yi++)
			{
				// compute span endpoints
				xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
				xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

				// compute starting points for u,v,w interpolants
				ui = ul + FIXP16_ROUND_UP;
				vi = vl + FIXP16_ROUND_UP;
				wi = wl + FIXP16_ROUND_UP;

				// compute u,v interpolants
				if ((dx = (xend - xstart))>0)
				{
					du = (ur - ul)/dx;
					dv = (vr - vl)/dx;
					dw = (wr - wl)/dx;
				} // end if
				else
				{
					du = (ur - ul);
					dv = (vr - vl);
					dw = (wr - wl);
				} // end else

				// draw span
				for (xi=xstart; xi < xend; xi++)
				{
					// write textel assume 8.8.8
					int tmpa;
					int r0 = ui >> FIXP16_SHIFT, 
						g0 = vi >> FIXP16_SHIFT, 
						b0 = wi >> FIXP16_SHIFT;
					int r1, g1, b1;
					_RGB8888FROM32BIT(screen_ptr[xi], &tmpa, &r1, &g1, &b1);
					r0 = r0 * alpha + r1 * (255 - alpha);
					g0 = g0 * alpha + g1 * (255 - alpha);
					b0 = b0 * alpha + b1 * (255 - alpha);
					screen_ptr[xi] = _RGB32BIT(255, r0 >> 8, g0 >> 8, b0 >> 8);

					// interpolate u,v
					ui+=du;
					vi+=dv;
					wi+=dw;
				} // end for xi

				// interpolate u,v,w,x along right and left edge
				xl+=dxdyl;
				ul+=dudyl;
				vl+=dvdyl;
				wl+=dwdyl;

				xr+=dxdyr;
				ur+=dudyr;
				vr+=dvdyr;
				wr+=dwdyr;

				// advance screen ptr
				screen_ptr+=mem_pitch;

				// test for yi hitting second region, if so change interpolant
				if (yi==yrestart)
				{
					// test interpolation side change flag

					if (irestart == INTERP_LHS)
					{
						// LHS
						dyl = (y2 - y1);	

						dxdyl = ((x2 - x1)   << FIXP16_SHIFT)/dyl;
						dudyl = ((tu2 - tu1) << FIXP16_SHIFT)/dyl;  
						dvdyl = ((tv2 - tv1) << FIXP16_SHIFT)/dyl;   		
						dwdyl = ((tw2 - tw1) << FIXP16_SHIFT)/dyl;   

						// set starting values
						xl = (x1  << FIXP16_SHIFT);
						ul = (tu1 << FIXP16_SHIFT);
						vl = (tv1 << FIXP16_SHIFT);
						wl = (tw1 << FIXP16_SHIFT);

						// interpolate down on LHS to even up
						xl+=dxdyl;
						ul+=dudyl;
						vl+=dvdyl;
						wl+=dwdyl;
					} // end if
					else
					{
						// RHS
						dyr = (y1 - y2);	

						dxdyr = ((x1 - x2)   << FIXP16_SHIFT)/dyr;
						dudyr = ((tu1 - tu2) << FIXP16_SHIFT)/dyr;  
						dvdyr = ((tv1 - tv2) << FIXP16_SHIFT)/dyr;   		
						dwdyr = ((tw1 - tw2) << FIXP16_SHIFT)/dyr;   

						// set starting values
						xr = (x2  << FIXP16_SHIFT);
						ur = (tu2 << FIXP16_SHIFT);
						vr = (tv2 << FIXP16_SHIFT);
						wr = (tw2 << FIXP16_SHIFT);

						// interpolate down on RHS to even up
						xr+=dxdyr;
						ur+=dudyr;
						vr+=dvdyr;
						wr+=dwdyr;
					} // end else

				} // end if

			} // end for y

		} // end else	

	} // end if

} // end Draw_Gouraud_Triangle16

}