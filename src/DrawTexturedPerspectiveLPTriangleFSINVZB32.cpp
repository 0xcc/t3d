#include "tools.h"
#include "tmath.h"
#include "defines.h"
#include "Modules.h"
#include "Graphics.h"
#include "Polygon.h"
#include "BmpImg.h"

namespace t3d {

void DrawTexturedPerspectiveLPTriangleFSINVZB32(PolygonF* face, unsigned char* _dest_buffer, 
												int mem_pitch, unsigned char* _zbuffer, int zpitch)
{
	// this function draws a textured triangle in 16-bit mode using a 1/z buffer and piecewise linear
	// perspective correct texture mappping, 1/z, u/z, v/z are interpolated down each edge then to draw
	// each span U and V are computed for each end point and the space is broken up into 32 pixel
	// spans where the correct U,V is computed at each point along the span, but linearly interpolated
	// across the span

	int v0=0,
		v1=1,
		v2=2,
		temp=0,
		tri_type = TRI_TYPE_NONE,
		irestart = INTERP_LHS;

	int dx,dy,dyl,dyr,      // general deltas
		u,v,z,
		du,dv,dz,
		xi,yi,              // the current interpolated x,y
		ui,vi,zi,           // the current interpolated u,v,z
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
		dzdyl,   
		zl,
		dudyr,
		ur,
		dvdyr,
		vr,
		dzdyr,
		zr;

	int ur2, ul2, vr2, vl2;

	int x0,y0,tu0,tv0,tz0,    // cached vertices
		x1,y1,tu1,tv1,tz1,
		x2,y2,tu2,tv2,tz2;

	unsigned int *screen_ptr  = NULL,
		*screen_line = NULL,
		*textmap     = NULL,
		*dest_buffer = (unsigned int *)_dest_buffer;

	unsigned int tmpa, r_base, g_base, b_base,
		r_textel, g_textel, b_textel, textel;


	unsigned int  *z_ptr = NULL,
		  *zbuffer = (unsigned int *)_zbuffer;

#ifdef DEBUG_ON
	// track rendering stats
	debug_polys_rendered_per_frame++;
#endif

	// extract texture map
	textmap = (unsigned int *)face->texture->Buffer();

	// extract base 2 of texture width
	int texture_shift2 = logbase2ofx[face->texture->Width()];

	// adjust memory pitch to words, divide by 2
	mem_pitch >>=2;

	// adjust zbuffer pitch for 32 bit alignment
	zpitch >>= 2;

	// apply fill convention to coordinates
	face->tvlist[0].x = (int)(face->tvlist[0].x+0.5);
	face->tvlist[0].y = (int)(face->tvlist[0].y+0.5);

	face->tvlist[1].x = (int)(face->tvlist[1].x+0.5);
	face->tvlist[1].y = (int)(face->tvlist[1].y+0.5);

	face->tvlist[2].x = (int)(face->tvlist[2].x+0.5);
	face->tvlist[2].y = (int)(face->tvlist[2].y+0.5);

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
	{std::swap(v0,v1);} 

	if (face->tvlist[v2].y < face->tvlist[v0].y) 
	{std::swap(v0,v2);}

	if (face->tvlist[v2].y < face->tvlist[v1].y) 
	{std::swap(v1,v2);}

	// now test for trivial flat sided cases
	if (FCMP(face->tvlist[v0].y, face->tvlist[v1].y) )
	{ 
		// set triangle type
		tri_type = TRI_TYPE_FLAT_TOP;

		// sort vertices left to right
		if (face->tvlist[v1].x < face->tvlist[v0].x) 
		{std::swap(v0,v1);}

	} // end if
	else
		// now test for trivial flat sided cases
		if (FCMP(face->tvlist[v1].y ,face->tvlist[v2].y))
		{ 
			// set triangle type
			tri_type = TRI_TYPE_FLAT_BOTTOM;

			// sort vertices left to right
			if (face->tvlist[v2].x < face->tvlist[v1].x) 
			{std::swap(v1,v2);}

		} // end if
		else
		{
			// must be a general triangle
			tri_type = TRI_TYPE_GENERAL;

		} // end else

		// extract base color of lit poly, so we can modulate texture a bit
		// for lighting
		_RGB8888FROM32BIT(face->lit_color[0], &tmpa, &r_base, &g_base, &b_base);

		// extract vertices for processing, now that we have order
		x0  = (int)(face->tvlist[v0].x+0.0);
		y0  = (int)(face->tvlist[v0].y+0.0);
		tu0 = ((int)(face->tvlist[v0].u0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v0].z+0.5);
		tv0 = ((int)(face->tvlist[v0].v0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v0].z+0.5);
		tz0 = (1 << FIXP28_SHIFT) / (int)(face->tvlist[v0].z+0.5);

		x1  = (int)(face->tvlist[v1].x+0.0);
		y1  = (int)(face->tvlist[v1].y+0.0);
		tu1 = ((int)(face->tvlist[v1].u0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v1].z+0.5);
		tv1 = ((int)(face->tvlist[v1].v0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v1].z+0.5);
		tz1 = (1 << FIXP28_SHIFT) / (int)(face->tvlist[v1].z+0.5);

		x2  = (int)(face->tvlist[v2].x+0.0);
		y2  = (int)(face->tvlist[v2].y+0.0);
		tu2 = ((int)(face->tvlist[v2].u0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v2].z+0.5);
		tv2 = ((int)(face->tvlist[v2].v0+0.5) << FIXP22_SHIFT) / (int)(face->tvlist[v2].z+0.5);
		tz2 = (1 << FIXP28_SHIFT) / (int)(face->tvlist[v2].z+0.5);


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
				dudyl = ((tu2 - tu0) << 0)/dy;  
				dvdyl = ((tv2 - tv0) << 0)/dy;    
				dzdyl = ((tz2 - tz0) << 0)/dy;    

				dxdyr = ((x2 - x1)   << FIXP16_SHIFT)/dy;
				dudyr = ((tu2 - tu1) << 0)/dy;  
				dvdyr = ((tv2 - tv1) << 0)/dy;   
				dzdyr = ((tz2 - tz1) << 0)/dy;  

				// test for y clipping
				if (y0 < min_clip_y)
				{
					// compute overclip
					dy = (min_clip_y - y0);

					// computer new LHS starting values
					xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
					ul = dudyl*dy + (tu0 << 0);
					vl = dvdyl*dy + (tv0 << 0);
					zl = dzdyl*dy + (tz0 << 0);

					// compute new RHS starting values
					xr = dxdyr*dy + (x1  << FIXP16_SHIFT);
					ur = dudyr*dy + (tu1 << 0);
					vr = dvdyr*dy + (tv1 << 0);
					zr = dzdyr*dy + (tz1 << 0);

					// compute new starting y
					ystart = min_clip_y;
				} // end if
				else
				{
					// no clipping

					// set starting values
					xl = (x0 << FIXP16_SHIFT);
					xr = (x1 << FIXP16_SHIFT);

					ul = (tu0 << 0);
					vl = (tv0 << 0);
					zl = (tz0 << 0);

					ur = (tu1 << 0);
					vr = (tv1 << 0);
					zr = (tz1 << 0);

					// set starting y
					ystart = y0;
				} // end else

			} // end if flat top
			else
			{
				// must be flat bottom

				// compute all deltas
				dy = (y1 - y0);

				dxdyl = ((x1 - x0)   << FIXP16_SHIFT)/dy;
				dudyl = ((tu1 - tu0) << 0)/dy;  
				dvdyl = ((tv1 - tv0) << 0)/dy;    
				dzdyl = ((tz1 - tz0) << 0)/dy;   

				dxdyr = ((x2 - x0)   << FIXP16_SHIFT)/dy;
				dudyr = ((tu2 - tu0) << 0)/dy;  
				dvdyr = ((tv2 - tv0) << 0)/dy;   
				dzdyr = ((tz2 - tz0) << 0)/dy;   

				// test for y clipping
				if (y0 < min_clip_y)
				{
					// compute overclip
					dy = (min_clip_y - y0);

					// computer new LHS starting values
					xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
					ul = dudyl*dy + (tu0 << 0);
					vl = dvdyl*dy + (tv0 << 0);
					zl = dzdyl*dy + (tz0 << 0);

					// compute new RHS starting values
					xr = dxdyr*dy + (x0  << FIXP16_SHIFT);
					ur = dudyr*dy + (tu0 << 0);
					vr = dvdyr*dy + (tv0 << 0);
					zr = dzdyr*dy + (tz0 << 0);

					// compute new starting y
					ystart = min_clip_y;
				} // end if
				else
				{
					// no clipping

					// set starting values
					xl = (x0 << FIXP16_SHIFT);
					xr = (x0 << FIXP16_SHIFT);

					ul = (tu0 << 0);
					vl = (tv0 << 0);
					zl = (tz0 << 0);

					ur = (tu0 << 0);
					vr = (tv0 << 0);
					zr = (tz0 << 0);

					// set starting y
					ystart = y0;
				} // end else	

			} // end else flat bottom

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

				// point zbuffer to starting line
				z_ptr = zbuffer + (ystart * zpitch);

				for (yi = ystart; yi < yend; yi++)
				{
					// compute span endpoints
					xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
					xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

					ul2 = ((ul << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
					ur2 = ((ur << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

					vl2 = ((vl << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
					vr2 = ((vr << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

					// compute starting points for u,v interpolants
					zi = zl + 0; // ????
					ui = ul2 + 0;
					vi = vl2 + 0;

					// compute u,v interpolants
					if ((dx = (xend - xstart))>0)
					{
						du = (ur2 - ul2) / dx;
						dv = (vr2 - vl2) / dx;
						dz = (zr - zl) / dx;
					} // end if
					else
					{
						du = (ur2 - ul2) ;
						dv = (vr2 - vl2) ;
						dz = (zr - zl);
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
						zi+=dx*dz;

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
						// test if z of current pixel is nearer than current z buffer value
						if (zi > z_ptr[xi])
						{
							// write textel
							// get textel first
							textel = textmap[ (ui >> FIXP22_SHIFT) + ( (vi >> FIXP22_SHIFT)  << texture_shift2)];

							// extract rgb components
							_RGB8888FROM32BIT(textel, &tmpa, &r_textel, &g_textel, &b_textel);

							// modulate textel with lit background color
							r_textel*=r_base; 
							g_textel*=g_base;
							b_textel*=b_base;

							// finally write pixel, note that we did the math such that the results are r*32, g*64, b*32
							// hence we need to divide the results by 32,64,32 respetively, BUT since we need to shift
							// the results to fit into the destination 5.6.5 word, we can take advantage of the shifts
							// and they all cancel out for the most part, but we will need logical anding, we will do
							// it later when we optimize more...
							screen_ptr[xi] = _RGB32BIT(255, r_textel >> 8, g_textel >> 8, b_textel >> 8);

							// update z-buffer
							z_ptr[xi] = zi;             
						} // end if


						// interpolate u,v,z
						ui+=du;
						vi+=dv;
						zi+=dz;
					} // end for xi

					// interpolate u,v,x along right and left edge
					xl+=dxdyl;
					ul+=dudyl;
					vl+=dvdyl;
					zl+=dzdyl;

					xr+=dxdyr;
					ur+=dudyr;
					vr+=dvdyr;
					zr+=dzdyr;

					// advance screen ptr
					screen_ptr+=mem_pitch;

					// advance zbuffer ptr
					z_ptr+=zpitch;
				} // end for y

			} // end if clip
			else
			{
				// non-clip version

				// point screen ptr to starting line
				screen_ptr = dest_buffer + (ystart * mem_pitch);

				// point zbuffer to starting line
				z_ptr = zbuffer + (ystart * zpitch);

				for (yi = ystart; yi < yend; yi++)
				{
					// compute span endpoints
					xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
					xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

					ul2 = ((ul << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
					ur2 = ((ur << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

					vl2 = ((vl << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
					vr2 = ((vr << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

					// compute starting points for u,v interpolants
					zi = zl + 0; // ????
					ui = ul2 + 0;
					vi = vl2 + 0;

					// compute u,v interpolants
					if ((dx = (xend - xstart))>0)
					{
						du = (ur2 - ul2) / dx;
						dv = (vr2 - vl2) / dx;
						dz = (zr - zl) / dx;
					} // end if
					else
					{
						du = (ur2 - ul2) ;
						dv = (vr2 - vl2) ;
						dz = (zr - zl);
					} // end else


					// draw span
					for (xi=xstart; xi < xend; xi++)
					{
						// test if z of current pixel is nearer than current z buffer value
						if (zi > z_ptr[xi])
						{
							// write textel
							// get textel first
							textel = textmap[ (ui >> FIXP22_SHIFT) + ( (vi >> FIXP22_SHIFT)  << texture_shift2)];

							// extract rgb components
							_RGB8888FROM32BIT(textel, &tmpa, &r_textel, &g_textel, &b_textel);

							// modulate textel with lit background color
							r_textel*=r_base; 
							g_textel*=g_base;
							b_textel*=b_base;

							// finally write pixel, note that we did the math such that the results are r*32, g*64, b*32
							// hence we need to divide the results by 32,64,32 respetively, BUT since we need to shift
							// the results to fit into the destination 5.6.5 word, we can take advantage of the shifts
							// and they all cancel out for the most part, but we will need logical anding, we will do
							// it later when we optimize more...
							screen_ptr[xi] = _RGB32BIT(255, r_textel >> 8, g_textel >> 8, b_textel >> 8);

							// update z-buffer
							z_ptr[xi] = zi;          
						} // end if

						// interpolate u,v,z
						ui+=du;
						vi+=dv;
						zi+=dz;
					} // end for xi

					// interpolate u,v,x along right and left edge
					xl+=dxdyl;
					ul+=dudyl;
					vl+=dvdyl;
					zl+=dzdyl;

					xr+=dxdyr;
					ur+=dudyr;
					vr+=dvdyr;
					zr+=dzdyr;

					// advance screen ptr
					screen_ptr+=mem_pitch;

					// advance zbuffer ptr
					z_ptr+=zpitch;

				} // end for y

			} // end if non-clipped

		} // end if
		else
			if (tri_type==TRI_TYPE_GENERAL)
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
					dudyl = ((tu2 - tu1) << 0)/dyl;  
					dvdyl = ((tv2 - tv1) << 0)/dyl;    
					dzdyl = ((tz2 - tz1) << 0)/dyl;  

					// RHS
					dyr = (y2 - y0);	

					dxdyr = ((x2  - x0)  << FIXP16_SHIFT)/dyr;
					dudyr = ((tu2 - tu0) << 0)/dyr;  
					dvdyr = ((tv2 - tv0) << 0)/dyr;   
					dzdyr = ((tz2 - tz0) << 0)/dyr;   

					// compute overclip
					dyr = (min_clip_y - y0);
					dyl = (min_clip_y - y1);

					// computer new LHS starting values
					xl = dxdyl*dyl + (x1  << FIXP16_SHIFT);
					ul = dudyl*dyl + (tu1 << 0);
					vl = dvdyl*dyl + (tv1 << 0);
					zl = dzdyl*dyl + (tz1 << 0);

					// compute new RHS starting values
					xr = dxdyr*dyr + (x0  << FIXP16_SHIFT);
					ur = dudyr*dyr + (tu0 << 0);
					vr = dvdyr*dyr + (tv0 << 0);
					zr = dzdyr*dyr + (tz0 << 0);

					// compute new starting y
					ystart = min_clip_y;

					// test if we need swap to keep rendering left to right
					if (dxdyr > dxdyl)
					{
						std::swap(dxdyl,dxdyr);
						std::swap(dudyl,dudyr);
						std::swap(dvdyl,dvdyr);
						std::swap(dzdyl,dzdyr);
						std::swap(xl,xr);
						std::swap(ul,ur);
						std::swap(vl,vr);
						std::swap(zl,zr);
						std::swap(x1,x2);
						std::swap(y1,y2);
						std::swap(tu1,tu2);
						std::swap(tv1,tv2);
						std::swap(tz1,tz2);

						// set interpolation restart
						irestart = INTERP_RHS;

					} // end if

				} // end if
				else
					if (y0 < min_clip_y)
					{
						// compute all deltas
						// LHS
						dyl = (y1 - y0);

						dxdyl = ((x1  - x0)  << FIXP16_SHIFT)/dyl;
						dudyl = ((tu1 - tu0) << 0)/dyl;  
						dvdyl = ((tv1 - tv0) << 0)/dyl;    
						dzdyl = ((tz1 - tz0) << 0)/dyl;  

						// RHS
						dyr = (y2 - y0);	

						dxdyr = ((x2  - x0)  << FIXP16_SHIFT)/dyr;
						dudyr = ((tu2 - tu0) << 0)/dyr;  
						dvdyr = ((tv2 - tv0) << 0)/dyr;   
						dzdyr = ((tz2 - tz0) << 0)/dyr;   

						// compute overclip
						dy = (min_clip_y - y0);

						// computer new LHS starting values
						xl = dxdyl*dy + (x0  << FIXP16_SHIFT);
						ul = dudyl*dy + (tu0 << 0);
						vl = dvdyl*dy + (tv0 << 0);
						zl = dzdyl*dy + (tz0 << 0);

						// compute new RHS starting values
						xr = dxdyr*dy + (x0  << FIXP16_SHIFT);
						ur = dudyr*dy + (tu0 << 0);
						vr = dvdyr*dy + (tv0 << 0);
						zr = dzdyr*dy + (tz0 << 0);

						// compute new starting y
						ystart = min_clip_y;

						// test if we need swap to keep rendering left to right
						if (dxdyr < dxdyl)
						{
							std::swap(dxdyl,dxdyr);
							std::swap(dudyl,dudyr);
							std::swap(dvdyl,dvdyr);
							std::swap(dzdyl,dzdyr);
							std::swap(xl,xr);
							std::swap(ul,ur);
							std::swap(vl,vr);
							std::swap(zl,zr);
							std::swap(x1,x2);
							std::swap(y1,y2);
							std::swap(tu1,tu2);
							std::swap(tv1,tv2);
							std::swap(tz1,tz2);

							// set interpolation restart
							irestart = INTERP_RHS;

						} // end if

					} // end if
					else
					{
						// no initial y clipping

						// compute all deltas
						// LHS
						dyl = (y1 - y0);

						dxdyl = ((x1  - x0)  << FIXP16_SHIFT)/dyl;
						dudyl = ((tu1 - tu0) << 0)/dyl;  
						dvdyl = ((tv1 - tv0) << 0)/dyl;    
						dzdyl = ((tz1 - tz0) << 0)/dyl;   

						// RHS
						dyr = (y2 - y0);	

						dxdyr = ((x2 - x0)   << FIXP16_SHIFT)/dyr;
						dudyr = ((tu2 - tu0) << 0)/dyr;  
						dvdyr = ((tv2 - tv0) << 0)/dyr;   		
						dzdyr = ((tz2 - tz0) << 0)/dyr;  

						// no clipping y

						// set starting values
						xl = (x0 << FIXP16_SHIFT);
						xr = (x0 << FIXP16_SHIFT);

						ul = (tu0 << 0);
						vl = (tv0 << 0);
						zl = (tz0 << 0);

						ur = (tu0 << 0);
						vr = (tv0 << 0);
						zr = (tz0 << 0);

						// set starting y
						ystart = y0;

						// test if we need swap to keep rendering left to right
						if (dxdyr < dxdyl)
						{
							std::swap(dxdyl,dxdyr);
							std::swap(dudyl,dudyr);
							std::swap(dvdyl,dvdyr);
							std::swap(dzdyl,dzdyr);
							std::swap(xl,xr);
							std::swap(ul,ur);
							std::swap(vl,vr);
							std::swap(zl,zr);
							std::swap(x1,x2);
							std::swap(y1,y2);
							std::swap(tu1,tu2);
							std::swap(tv1,tv2);
							std::swap(tz1,tz2);

							// set interpolation restart
							irestart = INTERP_RHS;

						} // end if

					} // end else

					// test for horizontal clipping
					if ((x0 < min_clip_x) || (x0 > max_clip_x) ||
						(x1 < min_clip_x) || (x1 > max_clip_x) ||
						(x2 < min_clip_x) || (x2 > max_clip_x))
					{
						// clip version
						// x clipping	

						// point screen ptr to starting line
						screen_ptr = dest_buffer + (ystart * mem_pitch);

						// point zbuffer to starting line
						z_ptr = zbuffer + (ystart * zpitch);

						for (yi = ystart; yi < yend; yi++)
						{
							// compute span endpoints
							xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
							xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

							ul2 = ((ul << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
							ur2 = ((ur << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

							vl2 = ((vl << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
							vr2 = ((vr << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

							// compute starting points for u,v interpolants
							zi = zl + 0; // ????
							ui = ul2 + 0;
							vi = vl2 + 0;

							// compute u,v interpolants
							if ((dx = (xend - xstart))>0)
							{
								du = (ur2 - ul2) / dx;
								dv = (vr2 - vl2) / dx;
								dz = (zr - zl) / dx;
							} // end if
							else
							{
								du = (ur2 - ul2) ;
								dv = (vr2 - vl2) ;
								dz = (zr - zl);
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
								zi+=dx*dz;

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
								// test if z of current pixel is nearer than current z buffer value
								if (zi > z_ptr[xi])
								{
									// write textel
									// get textel first
									textel = textmap[ (ui >> FIXP22_SHIFT) + ( (vi >> FIXP22_SHIFT)  << texture_shift2)];

									// extract rgb components
									_RGB8888FROM32BIT(textel, &tmpa, &r_textel, &g_textel, &b_textel);

									// modulate textel with lit background color
									r_textel*=r_base; 
									g_textel*=g_base;
									b_textel*=b_base;

									// finally write pixel, note that we did the math such that the results are r*32, g*64, b*32
									// hence we need to divide the results by 32,64,32 respetively, BUT since we need to shift
									// the results to fit into the destination 5.6.5 word, we can take advantage of the shifts
									// and they all cancel out for the most part, but we will need logical anding, we will do
									// it later when we optimize more...
									screen_ptr[xi] = _RGB32BIT(255, r_textel >> 8, g_textel >> 8, b_textel >> 8);

									// update z-buffer
									z_ptr[xi] = zi;           
								} // end if

								// interpolate u,v,z
								ui+=du;
								vi+=dv;
								zi+=dz;
							} // end for xi

							// interpolate u,v,x along right and left edge
							xl+=dxdyl;
							ul+=dudyl;
							vl+=dvdyl;
							zl+=dzdyl;

							xr+=dxdyr;
							ur+=dudyr;
							vr+=dvdyr;
							zr+=dzdyr;

							// advance screen ptr
							screen_ptr+=mem_pitch;

							// advance zbuffer ptr
							z_ptr+=zpitch;

							// test for yi hitting second region, if so change interpolant
							if (yi==yrestart)
							{
								// test interpolation side change flag

								if (irestart == INTERP_LHS)
								{
									// LHS
									dyl = (y2 - y1);	

									dxdyl = ((x2 - x1)   << FIXP16_SHIFT)/dyl;
									dudyl = ((tu2 - tu1) << 0)/dyl;  
									dvdyl = ((tv2 - tv1) << 0)/dyl;   		
									dzdyl = ((tz2 - tz1) << 0)/dyl;   

									// set starting values
									xl = (x1  << FIXP16_SHIFT);
									ul = (tu1 << 0);
									vl = (tv1 << 0);
									zl = (tz1 << 0);

									// interpolate down on LHS to even up
									xl+=dxdyl;
									ul+=dudyl;
									vl+=dvdyl;
									zl+=dzdyl;
								} // end if
								else
								{
									// RHS
									dyr = (y1 - y2);	

									dxdyr = ((x1 - x2)   << FIXP16_SHIFT)/dyr;
									dudyr = ((tu1 - tu2) << 0)/dyr;  
									dvdyr = ((tv1 - tv2) << 0)/dyr;   		
									dzdyr = ((tz1 - tz2) << 0)/dyr;  

									// set starting values
									xr = (x2  << FIXP16_SHIFT);
									ur = (tu2 << 0);
									vr = (tv2 << 0);
									zr = (tz2 << 0);

									// interpolate down on RHS to even up
									xr+=dxdyr;
									ur+=dudyr;
									vr+=dvdyr;
									zr+=dzdyr;

								} // end else

							} // end if

						} // end for y

					} // end if
					else
					{
						// no x clipping
						// point screen ptr to starting line
						screen_ptr = dest_buffer + (ystart * mem_pitch);

						// point zbuffer to starting line
						z_ptr = zbuffer + (ystart * zpitch);

						for (yi = ystart; yi < yend; yi++)
						{
							// compute span endpoints
							xstart = ((xl + FIXP16_ROUND_UP) >> FIXP16_SHIFT);
							xend   = ((xr + FIXP16_ROUND_UP) >> FIXP16_SHIFT);

							ul2 = ((ul << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
							ur2 = ((ur << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

							vl2 = ((vl << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zl >> 6) ) << 16;
							vr2 = ((vr << (FIXP28_SHIFT - FIXP22_SHIFT)) / (zr >> 6) ) << 16;

							// compute starting points for u,v interpolants
							zi = zl + 0; // ????
							ui = ul2 + 0;
							vi = vl2 + 0;

							// compute u,v interpolants
							if ((dx = (xend - xstart))>0)
							{
								du = (ur2 - ul2) / dx;
								dv = (vr2 - vl2) / dx;
								dz = (zr - zl) / dx;
							} // end if
							else
							{
								du = (ur2 - ul2) ;
								dv = (vr2 - vl2) ;
								dz = (zr - zl);
							} // end else

							// draw span
							for (xi=xstart; xi < xend; xi++)
							{
								// test if z of current pixel is nearer than current z buffer value
								if (zi > z_ptr[xi])
								{
									// write textel
									// get textel first
									textel = textmap[ (ui >> FIXP22_SHIFT) + ( (vi >> FIXP22_SHIFT)  << texture_shift2)];

									// extract rgb components
									_RGB8888FROM32BIT(textel, &tmpa, &r_textel, &g_textel, &b_textel);

									// modulate textel with lit background color
									r_textel*=r_base; 
									g_textel*=g_base;
									b_textel*=b_base;

									// finally write pixel, note that we did the math such that the results are r*32, g*64, b*32
									// hence we need to divide the results by 32,64,32 respetively, BUT since we need to shift
									// the results to fit into the destination 5.6.5 word, we can take advantage of the shifts
									// and they all cancel out for the most part, but we will need logical anding, we will do
									// it later when we optimize more...
									screen_ptr[xi] = _RGB32BIT(255, r_textel >> 8, g_textel >> 8, b_textel >> 8);

									// update z-buffer
									z_ptr[xi] = zi;          
								} // end if

								// interpolate u,v
								ui+=du;
								vi+=dv;
								zi+=dz;
							} // end for xi

							// interpolate u,v,x along right and left edge
							xl+=dxdyl;
							ul+=dudyl;
							vl+=dvdyl;
							zl+=dzdyl;

							xr+=dxdyr;
							ur+=dudyr;
							vr+=dvdyr;
							zr+=dzdyr;

							// advance screen ptr
							screen_ptr+=mem_pitch;

							// advance zbuffer ptr
							z_ptr+=zpitch;

							// test for yi hitting second region, if so change interpolant
							if (yi==yrestart)
							{
								// test interpolation side change flag

								if (irestart == INTERP_LHS)
								{
									// LHS
									dyl = (y2 - y1);	

									dxdyl = ((x2 - x1)   << FIXP16_SHIFT)/dyl;
									dudyl = ((tu2 - tu1) << 0)/dyl;  
									dvdyl = ((tv2 - tv1) << 0)/dyl;   		
									dzdyl = ((tz2 - tz1) << 0)/dyl;   

									// set starting values
									xl = (x1  << FIXP16_SHIFT);
									ul = (tu1 << 0);
									vl = (tv1 << 0);
									zl = (tz1 << 0);

									// interpolate down on LHS to even up
									xl+=dxdyl;
									ul+=dudyl;
									vl+=dvdyl;
									zl+=dzdyl;
								} // end if
								else
								{
									// RHS
									dyr = (y1 - y2);	

									dxdyr = ((x1 - x2)   << FIXP16_SHIFT)/dyr;
									dudyr = ((tu1 - tu2) << 0)/dyr;  
									dvdyr = ((tv1 - tv2) << 0)/dyr;   		
									dzdyr = ((tz1 - tz2) << 0)/dyr; 

									// set starting values
									xr = (x2  << FIXP16_SHIFT);
									ur = (tu2 << 0);
									vr = (tv2 << 0);
									zr = (tz2 << 0);

									// interpolate down on RHS to even up
									xr+=dxdyr;
									ur+=dudyr;
									vr+=dvdyr;
									zr+=dzdyr;

								} // end else

							} // end if

						} // end for y

					} // end else	

			} // end if
}

}