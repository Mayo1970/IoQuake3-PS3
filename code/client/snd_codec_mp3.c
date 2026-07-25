/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// MP3 support is enabled by this define
#ifdef USE_CODEC_MP3

// includes for the Q3 sound system
#include "client.h"
#include "snd_codec.h"

// minimp3 (public domain, single-header, no libc allocations required)
#define MINIMP3_IMPLEMENTATION
#include <minimp3.h>

#define MP3_SAMPLEWIDTH 2

// how many compressed bytes we keep buffered ahead of the decoder;
// large enough to always contain at least one full MP3 frame
#define MP3_READBUF_SIZE (16 * 1024)

// refill threshold: largest possible single MP3 frame (MAX_FREE_FORMAT_FRAME_SIZE
// in minimp3.h), so the decoder never starves mid-frame while data remains
#define MP3_MIN_LOOKAHEAD 2304

typedef struct
{
	mp3dec_t	dec;
	byte		readBuf[MP3_READBUF_SIZE];
	int			bufBytes;	// valid bytes currently in readBuf
	int			bufPos;		// consumed-so-far offset into readBuf
	qboolean	eof;
} mp3_decoder_t;

snd_codec_t mp3_codec =
{
	"mp3",
	S_MP3_CodecLoad,
	S_MP3_CodecOpenStream,
	S_MP3_CodecReadStream,
	S_MP3_CodecCloseStream,
	NULL
};

/*
=================
S_MP3_FillBuffer

Shifts any unconsumed bytes to the front of readBuf and tops it back
up from the underlying file so the decoder always sees as much
lookahead as possible.
=================
*/
static void S_MP3_FillBuffer(snd_stream_t *stream)
{
	mp3_decoder_t *mp3 = (mp3_decoder_t *) stream->ptr;
	int remaining;
	int wantBytes;
	int readBytes;

	if(mp3->eof)
		return;

	remaining = mp3->bufBytes - mp3->bufPos;

	if(mp3->bufPos > 0 && remaining > 0)
		memmove(mp3->readBuf, mp3->readBuf + mp3->bufPos, remaining);

	mp3->bufBytes = remaining;
	mp3->bufPos = 0;

	wantBytes = MP3_READBUF_SIZE - mp3->bufBytes;
	if(wantBytes <= 0)
		return;

	readBytes = FS_Read(mp3->readBuf + mp3->bufBytes, wantBytes, stream->file);
	if(readBytes > 0)
	{
		mp3->bufBytes += readBytes;
		stream->pos += readBytes;
	}

	if(readBytes < wantBytes)
		mp3->eof = qtrue;
}

/*
=================
S_MP3_CodecOpenStream
=================
*/
snd_stream_t *S_MP3_CodecOpenStream(const char *filename)
{
	snd_stream_t *stream;
	mp3_decoder_t *mp3;
	mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	mp3dec_frame_info_t info;
	int samples;

	if(!filename)
		return NULL;

	stream = S_CodecUtilOpen(filename, &mp3_codec);
	if(!stream)
		return NULL;

	mp3 = Z_Malloc(sizeof(mp3_decoder_t));
	if(!mp3)
	{
		S_CodecUtilClose(&stream);
		return NULL;
	}

	Com_Memset(mp3, 0, sizeof(mp3_decoder_t));
	mp3dec_init(&mp3->dec);
	stream->ptr = mp3;
	stream->pos = 0;

	// probe the first frame to fetch rate/channels
	S_MP3_FillBuffer(stream);

	samples = mp3dec_decode_frame(&mp3->dec, mp3->readBuf + mp3->bufPos,
			mp3->bufBytes - mp3->bufPos, pcm, &info);

	if(info.channels <= 0 || info.hz <= 0)
	{
		Z_Free(mp3);
		S_CodecUtilClose(&stream);
		return NULL;
	}

	// rewind the decoder frame consumption so the first frame is
	// decoded again from S_MP3_CodecReadStream (info.frame_bytes may be
	// 0 on a probe-only/garbage-skip call, so don't advance bufPos here)
	mp3dec_init(&mp3->dec);

	stream->info.rate = info.hz;
	stream->info.width = MP3_SAMPLEWIDTH;
	stream->info.channels = info.channels;
	// total sample count is not known up-front without a full scan;
	// background music/one-shot playback only consume info.size via
	// streamed reads, so leave it unset (0) like other streaming-only data
	stream->info.samples = 0;
	stream->info.size = 0;
	stream->info.dataofs = 0;

	(void) samples;

	return stream;
}

/*
=================
S_MP3_CodecCloseStream
=================
*/
void S_MP3_CodecCloseStream(snd_stream_t *stream)
{
	if(!stream)
		return;

	Z_Free(stream->ptr);
	S_CodecUtilClose(&stream);
}

/*
=================
S_MP3_CodecReadStream
=================
*/
int S_MP3_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer)
{
	mp3_decoder_t *mp3;
	byte *bufPtr;
	int bytesWritten;
	mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	mp3dec_frame_info_t info;
	int samples;
	int frameBytes;

	if(!(stream && buffer))
		return 0;

	if(bytes <= 0)
		return 0;

	mp3 = (mp3_decoder_t *) stream->ptr;
	bufPtr = (byte *) buffer;
	bytesWritten = 0;

	while(bytesWritten < bytes)
	{
		if(mp3->bufBytes - mp3->bufPos < MP3_MIN_LOOKAHEAD && !mp3->eof)
			S_MP3_FillBuffer(stream);

		if(mp3->bufBytes - mp3->bufPos <= 0)
			break;

		samples = mp3dec_decode_frame(&mp3->dec, mp3->readBuf + mp3->bufPos,
				mp3->bufBytes - mp3->bufPos, pcm, &info);

		if(info.frame_bytes <= 0)
			break;

		mp3->bufPos += info.frame_bytes;

		if(samples > 0)
		{
			frameBytes = samples * info.channels * MP3_SAMPLEWIDTH;

			if(bytesWritten + frameBytes > bytes)
				frameBytes = bytes - bytesWritten;

			Com_Memcpy(bufPtr + bytesWritten, pcm, frameBytes);
			bytesWritten += frameBytes;
		}
	}

	return bytesWritten;
}

/*
=====================================================================
S_MP3_CodecLoad

We handle S_MP3_CodecLoad as a special case of the streaming functions
where we read the whole stream at once.
======================================================================
*/
void *S_MP3_CodecLoad(const char *filename, snd_info_t *info)
{
	snd_stream_t *stream;
	byte *buffer;
	int allocSize;
	int bytesRead;
	int totalRead;

	if(!(filename && info))
		return NULL;

	stream = S_MP3_CodecOpenStream(filename);
	if(!stream)
		return NULL;

	// unlike OGG, total decoded size isn't known up-front without a full
	// scan; estimate a generous ceiling from the compressed file size
	// (worst-case low-bitrate MP3s expand well under 12x to 22kHz 16-bit
	// PCM) so the common case needs only a single Hunk temp allocation
	allocSize = stream->length * 12 + 64 * 1024;
	buffer = Hunk_AllocateTempMemory(allocSize);
	if(!buffer)
	{
		S_MP3_CodecCloseStream(stream);
		return NULL;
	}

	totalRead = 0;

	for(;;)
	{
		if(totalRead >= allocSize)
			break;

		bytesRead = S_MP3_CodecReadStream(stream, allocSize - totalRead, buffer + totalRead);
		if(bytesRead <= 0)
			break;

		totalRead += bytesRead;
	}

	if(totalRead <= 0)
	{
		Hunk_FreeTempMemory(buffer);
		S_MP3_CodecCloseStream(stream);
		return NULL;
	}

	info->rate = stream->info.rate;
	info->width = stream->info.width;
	info->channels = stream->info.channels;
	info->size = totalRead;
	info->samples = totalRead / (info->width * info->channels);
	info->dataofs = 0;

	S_MP3_CodecCloseStream(stream);

	return buffer;
}

#endif // USE_CODEC_MP3
