#include "Sound.h"

namespace t3d {

#define DSVOLUME_TO_DB(volume) ((DWORD)(-30*(100 - volume)))

Sound::Sound()
	: lpds(NULL)
	, lpdsbprimary(NULL)
{
}

int Sound::Init(HWND handle)
{
	// this function initializes the sound system
	static int first_time = 1; // used to track the first time the function
	// is entered

	// test for very first time
	if (first_time)
	{		
		// clear everything out
		memset(sound_fx,0,sizeof(pcm_sound)*MAX_SOUNDS);

		// reset first time
		first_time = 0;

		// create a directsound object
		if (FAILED(DirectSoundCreate(NULL, &lpds, NULL)))
			return(0);

		// set cooperation level
		if (FAILED(lpds->SetCooperativeLevel(handle,DSSCL_NORMAL)))
			return(0);
	}

	// initialize the sound fx array
	for (int index=0; index<MAX_SOUNDS; index++)
	{
		// test if this sound has been loaded
		if (sound_fx[index].dsbuffer)
		{
			// stop the sound
			sound_fx[index].dsbuffer->Stop();

			// release the buffer
			sound_fx[index].dsbuffer->Release();
		}

		// clear the record out
		memset(&sound_fx[index],0,sizeof(pcm_sound));

		// now set up the fields
		sound_fx[index].state = SOUND_NULL;
		sound_fx[index].id    = index;
	}

	return(1);
}

int Sound::Shutdown()
{
	// this function releases all the memory allocated and the directsound object
	// itself

	// first turn all sounds off
	StopAllSounds();

	// now release all sound buffers
	for (int index=0; index<MAX_SOUNDS; index++)
		if (sound_fx[index].dsbuffer)
			sound_fx[index].dsbuffer->Release();

	// now release the directsound interface itself
	if (lpds)
		lpds->Release();

	return(1);
}

int Sound::LoadWAV(char *filename, int control_flags)
{
	// this function loads a .wav file, sets up the directsound 
	// buffer and loads the data into memory, the function returns 
	// the id number of the sound

	HMMIO 			hwav;    // handle to wave file
	MMCKINFO		parent,  // parent chunk
		child;   // child chunk
	WAVEFORMATEX    wfmtx;   // wave format structure

	int	sound_id = -1,       // id of sound to be loaded
		index;               // looping variable

	unsigned char *snd_buffer,       // temporary sound buffer to hold voc data
		*audio_ptr_1=NULL, // data ptr to first write buffer 
		*audio_ptr_2=NULL; // data ptr to second write buffer

	DWORD audio_length_1=0,  // length of first write buffer
		audio_length_2=0;  // length of second write buffer

	// step one: are there any open id's ?
	for (index=0; index < MAX_SOUNDS; index++)
	{	
		// make sure this sound is unused
		if (sound_fx[index].state==SOUND_NULL)
		{
			sound_id = index;
			break;
		}
	}

	// did we get a free id?
	if (sound_id==-1)
		return(-1);

	// set up chunk info structure
	parent.ckid 	    = (FOURCC)0;
	parent.cksize 	    = 0;
	parent.fccType	    = (FOURCC)0;
	parent.dwDataOffset = 0;
	parent.dwFlags		= 0;

	// copy data
	child = parent;

	// open the WAV file
	if ((hwav = mmioOpen(filename, NULL, MMIO_READ | MMIO_ALLOCBUF))==NULL)
		return(-1);

	// descend into the RIFF 
	parent.fccType = mmioFOURCC('W', 'A', 'V', 'E');

	if (mmioDescend(hwav, &parent, NULL, MMIO_FINDRIFF))
	{
		// close the file
		mmioClose(hwav, 0);

		// return error, no wave section
		return(-1); 	
	}

	// descend to the WAVEfmt 
	child.ckid = mmioFOURCC('f', 'm', 't', ' ');

	if (mmioDescend(hwav, &child, &parent, 0))
	{
		// close the file
		mmioClose(hwav, 0);

		// return error, no format section
		return(-1); 	
	}

	// now read the wave format information from file
	if (mmioRead(hwav, (char *)&wfmtx, sizeof(wfmtx)) != sizeof(wfmtx))
	{
		// close file
		mmioClose(hwav, 0);

		// return error, no wave format data
		return(-1);
	}

	// make sure that the data format is PCM
	if (wfmtx.wFormatTag != WAVE_FORMAT_PCM)
	{
		// close the file
		mmioClose(hwav, 0);

		// return error, not the right data format
		return(-1); 
	}

	// now ascend up one level, so we can access data chunk
	if (mmioAscend(hwav, &child, 0))
	{
		// close file
		mmioClose(hwav, 0);

		// return error, couldn't ascend
		return(-1); 	
	}

	// descend to the data chunk 
	child.ckid = mmioFOURCC('d', 'a', 't', 'a');

	if (mmioDescend(hwav, &child, &parent, MMIO_FINDCHUNK))
	{
		// close file
		mmioClose(hwav, 0);

		// return error, no data
		return(-1); 	
	}

	// finally!!!! now all we have to do is read the data in and
	// set up the directsound buffer

	// allocate the memory to load sound data
	snd_buffer = (unsigned char *)malloc(child.cksize);

	// read the wave data 
	mmioRead(hwav, (char *)snd_buffer, child.cksize);

	// close the file
	mmioClose(hwav, 0);

	// set rate and size in data structure
	sound_fx[sound_id].rate  = wfmtx.nSamplesPerSec;
	sound_fx[sound_id].size  = child.cksize;
	sound_fx[sound_id].state = SOUND_LOADED;

	// set up the format data structure
	memset(&pcmwf, 0, sizeof(WAVEFORMATEX));

	pcmwf.wFormatTag	  = WAVE_FORMAT_PCM;  // pulse code modulation
	pcmwf.nChannels		  = 1;                // mono 
	pcmwf.nSamplesPerSec  = 11025;            // always this rate
	pcmwf.nBlockAlign	  = 1;                
	pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.wBitsPerSample  = 8;
	pcmwf.cbSize		  = 0;

	// prepare to create sounds buffer
	dsbd.dwSize			= sizeof(DSBUFFERDESC);
	dsbd.dwFlags		= control_flags | DSBCAPS_STATIC | DSBCAPS_LOCSOFTWARE;
	dsbd.dwBufferBytes	= child.cksize;
	dsbd.lpwfxFormat	= &pcmwf;

	// create the sound buffer
	if (FAILED(lpds->CreateSoundBuffer(&dsbd,&sound_fx[sound_id].dsbuffer,NULL)))
	{
		// release memory
		free(snd_buffer);

		// return error
		return(-1);
	}

	// copy data into sound buffer
	if (FAILED(sound_fx[sound_id].dsbuffer->Lock(0,					 
		child.cksize,			
		(void **) &audio_ptr_1, 
		&audio_length_1,
		(void **)&audio_ptr_2, 
		&audio_length_2,
		DSBLOCK_FROMWRITECURSOR)))
		return(0);

	// copy first section of circular buffer
	memcpy(audio_ptr_1, snd_buffer, audio_length_1);

	// copy last section of circular buffer
	memcpy(audio_ptr_2, (snd_buffer+audio_length_1),audio_length_2);

	// unlock the buffer
	if (FAILED(sound_fx[sound_id].dsbuffer->Unlock(audio_ptr_1, 
		audio_length_1, 
		audio_ptr_2, 
		audio_length_2)))
		return(0);

	// release the temp buffer
	free(snd_buffer);

	return(sound_id);
}

int Sound::ReplicateSound(int source_id)
{
	// this function replicates the sent sound and sends back the
	// id of the replicated sound, you would use this function
	// to make multiple copies of a gunshot or something that
	// you want to play multiple times simulataneously, but you
	// only want to load once

	if (source_id!=-1)
	{
		// duplicate the sound buffer
		// first hunt for an open id

		for (int id=0; id < MAX_SOUNDS; id++)
		{
			// is this sound open?
			if (sound_fx[id].state==SOUND_NULL)
			{
				// first make an identical copy
				sound_fx[id] = sound_fx[source_id];

				// now actually replicate the directsound buffer
				if (FAILED(lpds->DuplicateSoundBuffer(sound_fx[source_id].dsbuffer,
					&sound_fx[id].dsbuffer)))
				{
					// reset sound to NULL
					sound_fx[id].dsbuffer = NULL;
					sound_fx[id].state    = SOUND_NULL;

					// return error
					return(-1);
				}

				// now fix up id
				sound_fx[id].id = id;

				// return replicated sound
				return(id);
			} 
		}
	}
	else
		return(-1);

	return(-1);
}

int Sound::Play(int id, int flags, int volume, int rate, int pan)
{
	// this function plays a sound, the only parameter that 
	// works is the flags which can be 0 to play once or
	// DSBPLAY_LOOPING

	if (sound_fx[id].dsbuffer)
	{
		// reset position to start
		if (FAILED(sound_fx[id].dsbuffer->SetCurrentPosition(0)))
			return(0);

		// play sound
		if (FAILED(sound_fx[id].dsbuffer->Play(0,0,flags)))
			return(0);
	}

	return(1);
}

int Sound::SetVolume(int id,int vol)
{
	// this function sets the volume on a sound 0-100
	if (sound_fx[id].dsbuffer->SetVolume(DSVOLUME_TO_DB(vol))!=DS_OK)
		return(0);

	return(1);
}

int Sound::SetFreq(int id,int freq)
{
	// this function sets the playback rate
	if (sound_fx[id].dsbuffer->SetFrequency(freq)!=DS_OK)
		return(0);

	return(1);
}

int Sound::SetPan(int id,int pan)
{
	// this function sets the pan, -10,000 to 10,0000
	if (sound_fx[id].dsbuffer->SetPan(pan)!=DS_OK)
		return(0);

	return(1);
}

int Sound::StopSound(int id)
{
	if (sound_fx[id].dsbuffer)
	{
		sound_fx[id].dsbuffer->Stop();
		sound_fx[id].dsbuffer->SetCurrentPosition(0);
	}

	return(1);
}

int Sound::DeleteAllSounds()
{
	for (int index=0; index < MAX_SOUNDS; index++)
		DeleteSound(index);

	return(1);
}

int Sound::DeleteSound(int id)
{
	// this function deletes a single sound and puts it back onto the available list

	// first stop it
	if (!StopSound(id))
		return(0);

	// now delete it
	if (sound_fx[id].dsbuffer)
	{
		// release the com object
		sound_fx[id].dsbuffer->Release();
		sound_fx[id].dsbuffer = NULL;

		return(1);
	}

	return(1);
}

int Sound::StopAllSounds()
{
	for (int index=0; index<MAX_SOUNDS; index++)
		StopSound(index);	
	return(1);
}

int Sound::StatusSound(int id)
{
	if (sound_fx[id].dsbuffer)
	{
		ULONG status; 
		sound_fx[id].dsbuffer->GetStatus(&status);
		return(status);
	}
	else
		return(-1);
}

}