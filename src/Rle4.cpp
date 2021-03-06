#include <windows.h>
/*------------------------------------------------------*/
#include "Rle4.h"
/*------------------------------------------------------*/
#ifndef IN_CUDA_ENV
#include <vector>
#include "VecMath.h"
#include "bmp.h"
#else
typedef float3 vec3f;
typedef int3 vec3i;
#endif
#include "Tree.h"
/*------------------------------------------------------*/
void RLE4::compress_all(Tree& tree)
{
	int m;
	Tree t[16]; 

	t[0] = tree;

	printf("MipVol0 : %d x %d x %d\n",t[0].vx,t[0].vy,t[0].vz);

	for (nummaps=0;nummaps<15;nummaps++)
	{
		m=nummaps;
		if(t[m].vx<4) break; 
		if(t[m].vy<4) break; 
		if(t[m].vz<4) break; 
		t[m+1].get_mipmap(t[m]);
		printf("MipVol%d : %d x %d x %d\n",m+1,t[m+1].vx,t[m+1].vy,t[m+1].vz);
	}
	nummaps++;
	printf("Num Mips:%d\n",nummaps);

	for (m=0;m<nummaps;m++)
	{
		printf("Compress MipVol %d\n",m);
		map[m]=compress(t[m],m);

		if(t[m].faces_arr.size()>0)
			t[m].colorize_map(map[m]);
		//t[m].exit();

		if(map[m].slabs_size==0){nummaps=m-1;break;}
	}

}
//----------------------------------------------------------------------------

Map4 RLE4::compress(Tree& tree,int mip_lvl)
{
	int scale = 1<<mip_lvl;
	int sx = tree.vx;
	int sy = tree.vy;
	int sz = tree.vz;

	uchar* mem =(uchar*)tree.voxel;
	uchar* col1=tree.voxel_col1;
	uchar* col2=tree.voxel_col2;

	Map4 map4;

	map4.sx=sx;
	map4.sy=sy;
	map4.sz=sz;
	int sxy = sx*sy;

	std::vector<ushort> slab;
	std::vector<ushort> texture;
	std::vector<uint> map;

	map.resize(sx*sz);

	bool store=false;
	
	for (int i=0;i<sx;i++)
	for (int k=0;k<sz;k++)
	{
		map[ i+k*sx ] = slab.size();

		int slab_len = slab.size(); slab.push_back(0);
		int tex_len  = slab.size(); slab.push_back(0);

		int  skip=0,solid=0;

		texture.clear();

		for (int j=0;j<sy+1;j++)
		{
			uchar f=0;
			
			if (j<sy) f=mem[(i+j*sx+k*sxy)>>3] & (1<<(i&7));

			store=false;

			int nx,ny,nz;
						
			if (f)
			{
				int cnt=0;
				int mx=0;
				int my=0;
				int mz=0;

				for (int a=-1;a<2;a++)
				for (int b=-1;b<2;b++)
				for (int c=-1;c<2;c++)
				{
					int x=i+a;
					int y=j+b;
					int z=k+c;

					if (x<0)continue;
					if (y<0)continue;
					if (z<0)continue;
					if (x>=sx)continue;
					if (y>=sy)continue;
					if (z>=sz)continue;

					uchar d=mem[(x+y*sx+z*sxy)>>3] & (1<<(x&7));

					if(d)
					{
						mx+=a;
						my+=b;
						mz+=c;
						cnt++;
					}
				}
				if (cnt<27) 
				{
					vec3f centroid( -mx,my,-mz );
					centroid.normalize();
					nx = int(float(127.0f*centroid.x + 128.0f)); if (nx>255)nx=255; if (nx<0)nx=0;
					ny = int(float(127.0f*centroid.y + 128.0f)); if (ny>255)ny=255; if (ny<0)ny=0;
					nz = int(float(127.0f*centroid.z + 128.0f)); if (nz>255)nz=255; if (nz<0)nz=0;
					int bit1=0,bit2=0;

					if(col1)
					{
						bit1 = (col1[(i+j*sx+k*sxy)>>3] & (1<<(i&7))) ? 256 : 0;
						bit2 = (col2[(i+j*sx+k*sxy)>>3] & (1<<(i&7))) ? 512 : 0;
					}
					
					int mat=(bit1|bit2)>>8;
					float rnd1=sin( 7*float(i*scale)*2*3.1415/sx)*cos(3 *float(j*scale)*2*3.1415/sy);
					float rnd2=sin( 3*float(j*scale)*2*3.1415/sy)*cos(5 *float(k*scale)*2*3.1415/sz);
					float rnd3=sin( 7*float(k*scale)*2*3.1415/sz)*cos(4 *float(i*scale)*2*3.1415/sx);
					int rnd4= (rnd1+rnd2+rnd3+3);
					
					
					//ny = ( ny * (rnd4+9)  ) >> 4;
					if(mat==0) ny = (ny * (((((i*scale)  ^ (k*scale ))>>4)&15)+15)) >> 5;
					if(mat==3) ny = (ny * (((((i*scale ) ^ (k*scale ))>>3)&15)+25)) >> 5;
					//ny=(ny*300)/255-25;
					if(ny>255)ny=255;									
					if(ny<0)ny=0;									

					texture.push_back(ny|bit1|bit2);
					store=true;
				}
			}

			if(solid>0)if(!store) 
			{
				while(skip>1023)
				{
					slab.push_back(1023);
					skip-=1023;
				}
				while(solid>63)	
				{
					slab.push_back(63*1024+(skip&1023));
					solid-=63;
					skip=0;
				}
				slab.push_back((solid&63)*1024+(skip&1023));
				solid=0;
				skip=0;
			}

			if(store) solid++; else skip++;
		}
		slab[slab_len]=slab.size()-(slab_len+2);
		slab[tex_len]=texture.size();
		for (int i=0;i<texture.size();i++)	slab.push_back(texture[i]);
	}
	map4.map = (uint*) malloc (sx*sz*4);
	memcpy(map4.map, &map[0], sx*sz*4);

	map4.slabs = (ushort*) malloc (slab.size()*2);
	memcpy(map4.slabs, &slab[0], slab.size()*2);

	map4.slabs_size=slab.size();
	return map4;
}
/*------------------------------------------------------*/
void RLE4::init()
{
	nummaps=0;
}
/*------------------------------------------------------*/
void RLE4::clear()
{
	for (int m=0;m<nummaps;m++)
	{
		if(map[m].map) free(map[m].map);
		if(map[m].slabs) free(map[m].slabs);
	}
	init();
}
/*------------------------------------------------------*/
void RLE4::save(char *filename)
{
	FILE* fn; 

	if ((fn = fopen (filename,"wb")) == NULL) return;
	printf("Saving %s\n",filename);

	fwrite(&nummaps,1,4,fn);

	if(nummaps==0){MessageBoxA(0,"0 mipmaps","error",0);exit(0);}

	for (int m=0;m<nummaps;m++)
	{
		fwrite(&map[m].sx,1,4,fn);
		fwrite(&map[m].sy,1,4,fn);
		fwrite(&map[m].sz,1,4,fn);
		fwrite(&map[m].slabs_size,1,4,fn);
		fwrite(map[m].slabs,1,map[m].slabs_size*2,fn);
	}

	fclose(fn);
}
/*------------------------------------------------------*/
bool RLE4::load(char *filename)
{
	FILE* fn; 

	if ((fn = fopen (filename,"rb")) == NULL)
	{
		MessageBox(0,filename,"File not found - creating started",0);
		return false;
	}

	printf("Loading %s\n",filename);

	fread(&nummaps,1,4,fn);

	int total_numrle=0;
	int total_numtex=0;
	int numtex_0=0;

	for (int m=0;m<nummaps;m++)
	{
		printf("MipVol %d ------------------------\n",m);

		fread(&map[m].sx,1,4,fn);
		fread(&map[m].sy,1,4,fn);
		fread(&map[m].sz,1,4,fn);
		fread(&map[m].slabs_size,1,4,fn);

		printf("sx %d\n",map[m].sx);
		printf("sy %d\n",map[m].sy);
		printf("sz %d\n",map[m].sz);
		printf("slabs_size %d\n",map[m].slabs_size);

		map[m].map = (uint*) malloc ( map[m].sx*map[m].sz*4*2 );
		map[m].slabs = (ushort*)malloc ( map[m].slabs_size*2 );

		fread(map[m].slabs,1,map[m].slabs_size*2,fn);

		int x=0,z=0;

		memset(map[m].map,0,map[m].sx*map[m].sz*4*2);
		map[m].map[0]=0;

		int ofs=0;

		int numrle = 0, numtex=0;

		while(1)
		{
			map[m].map[(x+z*map[m].sx)*2]=ofs;

			uint count    = map[m].slabs[ofs]; 
			uint firstrle = 0;
			if(ofs+2<map[m].slabs_size) firstrle = map[m].slabs[ofs+2]; 

			map[m].map[(x+z*map[m].sx)*2+1]= count + (firstrle<<16);

			x=(x+1)%map[m].sx;
			if(x==0)
			{
				z=(z+1)%map[m].sz;
				if(z==0)break;
			}
			numrle += map[m].slabs[ofs]+2;
			numtex += map[m].slabs[ofs+1];
			ofs+=map[m].slabs[ofs]+map[m].slabs[ofs+1]+2;
		}

		total_numrle+=numrle;
		total_numtex+=numtex;

		if (m==0) numtex_0 = numtex;

		printf("Number of RLE Elements:%d\n",numrle);
		printf("Number of Voxels:%d\n",numtex);
		printf("Bits per Voxel:%2.2f (RLE)+ 16 (Color)\n",float(numrle*16)/float(numtex));
	}

	printf("Total Number of RLE Elements:%d\n",total_numrle);
	printf("Total Number of Voxels:%d\n",total_numtex);
	printf("Total Bits per Voxel:%2.2f (RLE)+ %2.2f (Color)\n",float(total_numrle*16)/float(numtex_0),float(total_numtex*16)/float(numtex_0));

	fclose(fn);
	return true;
}
/*------------------------------------------------------*/
float f_min(float a,float b) { return (a<b)? a : b ; }
float f_max(float a,float b) { return (a>b)? a : b ; }

bool RLE4::load_m5(char *filename)
{
	if (!load(filename)) exit(0);
	
	Map4 map4=map[0];
	
	Bmp bmp(map4.sx,map4.sy,24,0);
	memset(bmp.data,255,map4.sx*map4.sy*3);

//	int slice = 0;
	
	for (int slice=0;slice<map4.sz;slice++)
	for (int x=0;x<map4.sx;x++)
	{
		if (x==0)printf("Building Octree:%d     \r",slice);

		uint ofs						= map4.map[(x+(map4.sx*slice))*2+0];
		uint count_firstRLE	= map4.map[(x+(map4.sx*slice))*2+1];
		ushort count    = count_firstRLE & 65535;
		ushort firstRLE = count_firstRLE >> 16;

		ushort *p = map4.slabs + ofs + 2;
		ushort *pt= p+count;//+2;

		uint y1=0,y2=0;

		vec3f color_tab[4];
		color_tab[0]=vec3f(0.8,1.0,0.3);
		color_tab[1]=vec3f(1.0,0.7,0.3);
		color_tab[2]=vec3f(1.5,0.8,0.1);
		color_tab[3]=vec3f(0.2,0.8,0.2);

		for (int s=0;s<count;s++,p++)
		{
			ushort slab = *p;
			
			y1+=slab&1023;
			y2=y1+(slab>>10);
			
			for (;y1<y2;y1++,pt++) if (y1<map4.sy)
			{
				ushort tcol=*pt;

				int color = ( tcol >> 8 ) & 3;
				int intensity = ((tcol & 255)*(tcol & 255))/255+10;

				uchar4 rgba;

				/*
				rgba.z = 8*(tcol & 31);//f_max(f_min( 255,  float (color_tab[color].x * intensity )),0);//( tcol     &31)*8;
				rgba.y = 8*((tcol >> 5)& 31);//f_max(f_min( 255,  float (color_tab[color].y * intensity )),0) ;;//((tcol>>5) &31)*8;
				rgba.x = 8*((tcol >> 10)& 31);//f_max(f_min( 255,  float (color_tab[color].z * intensity )),0) ;;//((tcol>>10)&31)*8;
				rgba.w = 0;
				*/
				

				rgba.x = f_max(f_min( 255,  float (color_tab[color].x * intensity )),0);//( tcol     &31)*8;
				rgba.y = f_max(f_min( 255,  float (color_tab[color].y * intensity )),0) ;;//((tcol>>5) &31)*8;
				rgba.z = f_max(f_min( 255,  float (color_tab[color].z * intensity )),0) ;;//((tcol>>10)&31)*8;
				rgba.w = 0;
				

				extern void set_voxel(uint x,uint y,uint z,uchar4 color);
				set_voxel( x,map4.sy-1-y1,slice, rgba );

//				bmp.data[(x+y1*map4.sx)*3+2]= f_min( 255,  float (color_tab[color].x * intensity ));//( tcol     &31)*8;
	//			bmp.data[(x+y1*map4.sx)*3+1]= f_min( 255,  float (color_tab[color].y * intensity )) ;;//((tcol>>5) &31)*8;
		//		bmp.data[(x+y1*map4.sx)*3+0]= f_min( 255,  float (color_tab[color].z * intensity )) ;;//((tcol>>10)&31)*8;
			}
		}
	}
	
	//bmp.save("../slice.bmp");
	
	return true;
}
/*------------------------------------------------------*/
	//Called at least once for every voxel of the board
	//issolid: 0:air, 1:solid
void RLE4::setgeom (long x, long y, long z, long issolid) { /*your code here*/ }
/*------------------------------------------------------*/
	//Called only for surface voxels
	//A surface voxel is any solid voxel with at least 1 air voxel
	//  on one of its 6 sides. All solid voxels at z=0 are automatically
	//  surface voxels, but this is not true for x=0, x=1023, y=0, y=1023,
	//  z=255 (I believe)
	//argb: 32-bit color, high byte is used for shading scale (can be ignored)
ushort* volu;
void RLE4::setcol (long x, long y, long z, long argb) 
{ /*printf("%d %d %d: %08x\n",x,y,z,argb);*/ 
	int ofs=x+z*1024+y*256*1024;

	uint rgb=argb;
	uint r = ((rgb >> 0) & 255)>>3;
	uint g = ((rgb >> 8) & 255)>>3;
	uint b = ((rgb >>16) & 255)>>3;
	volu[ofs]=r+(g<<5)+(b<<10)+(1<<15);
}
/*------------------------------------------------------*/
long RLE4::loadvxl (char *filnam)
{
	struct dpoint3d{ double x, y, z; } ;
	dpoint3d ipos, istr, ihei, ifor;

	FILE *fil;
	long i, x, y, z;
	unsigned char *v, *vbuf;

	fil = fopen(filnam,"rb"); if (!fil) return(-1);
	fread(&i,4,1,fil); if (i != 0x09072000) return(-1);
	fread(&i,4,1,fil); if (i != 1024) return(-1);
	fread(&i,4,1,fil); if (i != 1024) return(-1);
	fread(&ipos,24,1,fil); //camera position
	fread(&istr,24,1,fil); //unit right vector
	fread(&ihei,24,1,fil); //unit down vector
	fread(&ifor,24,1,fil); //unit forward vector

	printf("loading %s...\n",filnam);

	//Allocate huge buffer and load rest of file into it...

	int p1=ftell(fil);
	fseek(fil, 0L, SEEK_END);
	int p2=ftell(fil);
	fseek(fil, p1, SEEK_SET);

	printf("reading %d bytes...\n",p2-p1);

	i = p2-p1;
	vbuf = (unsigned char *)malloc(i); if (!vbuf) { fclose(fil); return(-1); }
	fread(vbuf,i,1,fil);
	fclose(fil);

	printf("clear volume\n");

	/*
	0 numskip
	1 start
	2 end1
	3 end2
	4 rgba
	8 rgba
	*/

	int scale = 0;//1<<mip_lvl;
	int sx = 1024;
	int sy = 256;
	int sz = 1024;

	ushort* volume=(ushort*)malloc(sx*sy*sz*2);
	if(!volume)return -1;

	memset(volume,0,sx*sy*sz*2);

	volu=volume;

	Map4 map4;

	map4.sx=sx;
	map4.sy=sy;
	map4.sz=sz;
	int sxy = sx*sy;

	printf("unpack volume\n");

	v = vbuf;
	for(y=0;y<1024;y++)
		for(x=0;x<1024;x++)
		{
			while (1)
			{
				for(z=v[1];z<=v[2];z++)
					setcol(x,y,z,*(long *)&v[(z-v[1]+1)<<2]);

				if (!v[0]) break; 
				
				z = v[2]-v[1]-v[0]+2; 
				v += v[0]*4;

				for(z+=v[3];z<v[3];z++) 
					setcol(x,y,z,*(long *)&v[(z-v[3])<<2]);
			}
			v += ((((long)v[2])-((long)v[1])+2)<<2);
		}

	free(vbuf);

	printf("compressing\n");
	Map4 m=compressvxl(volume,1024,256,1024,0);

	map[0]=m;
	nummaps=1;
	return 0;
}
/*------------------------------------------------------*/
long RLE4::loadvxl_octree (char *filnam)
{
	struct dpoint3d{ double x, y, z; } ;
	dpoint3d ipos, istr, ihei, ifor;

	FILE *fil;
	long i, x, y, z;
	unsigned char *v, *vbuf;

	fil = fopen(filnam,"rb"); if (!fil) return(-1);
	fread(&i,4,1,fil); if (i != 0x09072000) return(-1);
	fread(&i,4,1,fil); if (i != 1024) return(-1);
	fread(&i,4,1,fil); if (i != 1024) return(-1);
	fread(&ipos,24,1,fil); //camera position
	fread(&istr,24,1,fil); //unit right vector
	fread(&ihei,24,1,fil); //unit down vector
	fread(&ifor,24,1,fil); //unit forward vector

	printf("loading %s...\n",filnam);

	//Allocate huge buffer and load rest of file into it...

	int p1=ftell(fil);
	fseek(fil, 0L, SEEK_END);
	int p2=ftell(fil);
	fseek(fil, p1, SEEK_SET);

	printf("reading %d bytes...\n",p2-p1);

	i = p2-p1;
	vbuf = (unsigned char *)malloc(i); if (!vbuf) { fclose(fil); return(-1); }
	fread(vbuf,i,1,fil);
	fclose(fil);

	printf("clear volume\n");

	/*
	0 numskip
	1 start
	2 end1
	3 end2
	4 rgba
	8 rgba
	*/

	int scale = 0;//1<<mip_lvl;
	int sx = 1024;
	int sy = 256;
	int sz = 1024;

	//ushort* volume=(ushort*)malloc(sx*sy*sz*2);
	//if(!volume)return -1;

	//memset(volume,0,sx*sy*sz*2);

	//volu=volume;

	Map4 map4;

	map4.sx=sx;
	map4.sy=sy;
	map4.sz=sz;
	int sxy = sx*sy;

	printf("unpack volume\n");

	v = vbuf;
	
	extern void set_voxel(uint x,uint y,uint z,uchar4 color);

	for(y=0;y<1024;y++)
		for(x=0;x<1024;x++)
		{
			while (1)
			{
				for(z=v[1];z<=v[2];z++)
				{
					uint ic = *(long *)&v[(z-v[1]+1)<<2];
					uchar4 c=*((uchar4*)&ic);
					uchar4 d;
					int dx=c.z*c.w / 125; if(dx>255)dx=255;
					int dy=c.y*c.w / 125; if(dy>255)dy=255;
					int dz=c.x*c.w / 125; if(dz>255)dz=255;
					d.x=dx;d.y=dy;d.z=dz;
					d.w=1;
					set_voxel( x,255-z,y,d);
						//x,map4.sy-1-y1,slice, rgba );
				}
				//	setcol(x,y,z,*(long *)&v[(z-v[1]+1)<<2]);

				if (!v[0]) break; 
				
				z = v[2]-v[1]-v[0]+2; 
				v += v[0]*4;

				for(z+=v[3];z<v[3];z++) 
				{
					//setcol(x,y,z,*(long *)&v[(z-v[3])<<2]);

					uint ic = *(long *)&v[(z-v[3])<<2];
					uchar4 c=*((uchar4*)&ic);
					uchar4 d;
					int dx=c.z*c.w / 125; if(dx>255)dx=255;
					int dy=c.y*c.w / 125; if(dy>255)dy=255;
					int dz=c.x*c.w / 125; if(dz>255)dz=255;
					d.x=dx;d.y=dy;d.z=dz;
					d.w=1;
					set_voxel( x,255-z,y,d);
				}
			}
			v += ((((long)v[2])-((long)v[1])+2)<<2);
		}

	free(vbuf);

//	printf("compressing\n");
//	Map4 m=compressvxl(volume,1024,256,1024,0);

	//map[0]=m;
	//nummaps=1;
	return 0;
}
/*------------------------------------------------------*/

Map4 RLE4::compressvxl(ushort* mem,int sx,int sy,int sz,int mip_lvl)//int sx,int sy,int sz,uchar* col1,uchar* col2);uchar* mem, int sx,int sy,int sz,uchar* col1,uchar* col2)
{
	int scale = 1<<mip_lvl;
//	int sx = tree.vx;
//	int sy = tree.vy;
//	int sz = tree.vz;

//	uchar* mem =(uchar*)tree.voxel;
//	uchar* col1=tree.voxel_col1;
//	uchar* col2=tree.voxel_col2;

	Map4 map4;

	map4.sx=sx;
	map4.sy=sy;
	map4.sz=sz;
	int sxy = sx*sy;

	std::vector<ushort> slab;
	std::vector<ushort> texture;
	std::vector<uint> map;

	map.resize(sx*sz);

	bool store=false;
	
	for (int i=0;i<sx;i++)
	for (int k=0;k<sz;k++)
	{
		map[ i+k*sx ] = slab.size();

		int slab_len = slab.size(); slab.push_back(0);
		int tex_len  = slab.size(); slab.push_back(0);

		int  skip=0,solid=0;

		texture.clear();

		for (int j=0;j<sy+1;j++)
		{
			ushort f=0;
			
			if (j<sy) f=mem[(i+j*sx+k*sxy)] & (1<<(15));

			store=false;

			int nx,ny,nz;
						
			if (f)
			{
				int cnt=0;
				int mx=0;
				int my=0;
				int mz=0;

				for (int a=-1;a<2;a++)
				for (int b=-1;b<2;b++)
				for (int c=-1;c<2;c++)
				{
					int x=i+a;
					int y=j+b;
					int z=k+c;

					if (x<0)continue;
					if (y<0)continue;
					if (z<0)continue;
					if (x>=sx)continue;
					if (y>=sy)continue;
					if (z>=sz)continue;

					ushort d=mem[(x+y*sx+z*sxy)] & (1<<15);

					if(d)
					{
						mx+=a;
						my+=b;
						mz+=c;
						cnt++;
					}
				}
				if (cnt<27) 
				{
					texture.push_back(mem[(i+j*sx+k*sxy)]&(32767));
					store=true;
				}
			}

			if(solid>0)if(!store) 
			{
				while(skip>1023)
				{
					slab.push_back(1023);
					skip-=1023;
				}
				while(solid>63)	
				{
					slab.push_back(63*1024+(skip&1023));
					solid-=63;
					skip=0;
				}
				slab.push_back((solid&63)*1024+(skip&1023));
				solid=0;
				skip=0;
			}

			if(store) solid++; else skip++;
		}
		slab[slab_len]=slab.size()-(slab_len+2);
		slab[tex_len]=texture.size();
		for (int i=0;i<texture.size();i++)	slab.push_back(texture[i]);
	}
	map4.map = (uint*) malloc (sx*sz*4);
	memcpy(map4.map, &map[0], sx*sz*4);

	map4.slabs = (ushort*) malloc (slab.size()*2);
	memcpy(map4.slabs, &slab[0], slab.size()*2);

	map4.slabs_size=slab.size();
	return map4;
}
/*------------------------------------------------------*/
