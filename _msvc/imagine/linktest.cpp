extern "C" {
#include <libavcodec/avcodec.h>
}

#include <OpenEXR/ImfInputFile.h>
#include <openjpeg.h>
#include <png.h>
#include <tiffio.h>
#include <turbojpeg.h>

void *linktest_func()
{
	static void *x[6];

	x[0] = &avcodec_open2;
	x[1] = new Imf::InputFile("NUL");
	x[2] = &opj_image_create;
	x[3] = &png_read_image;
	x[4] = &TIFFOpen;
	x[5] = &tjDecompress2;

	return x;
}
