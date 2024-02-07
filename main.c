#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>

static int fdProgrammer;

enum Flags_t
{
	FLAG_DOWNLOAD = 1 << 4,
	FLAG_DL_EXTENDED = 1 << 5,
	FLAG_DL_EXTPAGE = 1 << 6,
	FLAG_DOWNLOAD_MASK = 0xF0,
	FLAG_UPLOAD_MASK = ~0xF0,
	FLAG_VERIFY = 1<<0,
	FLAG_RETAIN = 1<<1

};

struct TotalState_t
{
	uint32_t TransferSize;
	uint32_t StartAddress;
	uint32_t BytesTransferred;
};
struct PartialReadData
{
	size_t BytesRead;
	size_t TotalBytesRead;
	size_t Offset;
	size_t BytesRemaining;
	void*  Buffer;
	void*  UserData;
};

static volatile bool s_interrupted = false;

typedef void (*sighandler_t)(int);
void SignalHandler(__attribute__((unused)) int signo)
{
	s_interrupted = true;
	struct termios atts;
	tcgetattr(fdProgrammer, &atts);
	cfsetispeed(&atts, B0); // Sends HUP, I think?
	cfsetospeed(&atts, B0);
	tcsetattr(fdProgrammer, TCSANOW, &atts);
	close(fdProgrammer);
	fdProgrammer = 0;
};

static bool ReadAll(int fd, void* out, size_t cbOut, void (*partialReadCallback)(const struct PartialReadData*), void* userData)
{
	struct PartialReadData data;
	data.BytesRemaining = cbOut;
	data.Offset = 0;
	data.TotalBytesRead = 0;
	data.Buffer = out;
	data.BytesRead = 0;
	data.UserData = userData;
	uint8_t* const pOut = (uint8_t*) out;
	while (data.BytesRemaining > 0)
	{
		int bytesRead = read(fd, &pOut[data.Offset], data.BytesRemaining);
		if (bytesRead <= 0)
			return false;
		data.BytesRead = bytesRead;
		data.TotalBytesRead += bytesRead;
		data.BytesRemaining -= bytesRead;
		if (partialReadCallback)
			partialReadCallback(&data);
		data.Offset += bytesRead;
	}
	return true;
}

static void RxCallback(const struct PartialReadData* data)
{
	struct TotalState_t* state = (struct TotalState_t*)data->UserData;
	size_t bytesRead = data->TotalBytesRead;
	if (data->BytesRemaining == 0)
		--bytesRead; // exclude terminating OK byte
	bytesRead += state->BytesTransferred;
	fprintf(stderr, "\033[1K\033[1G%zu/%u bytes transferred", bytesRead, state->TransferSize);
	fflush(stderr);
	return;
}

int Usage(const char* argv0)
{
	FILE* out = isatty(STDOUT_FILENO) ? stdout : stderr;
	fprintf(out, "USAGE: %s FILE [options]\n", argv0);
	fprintf(out, "       cat dataToUpload.bin | %s FILE [options]\n", argv0);
	fprintf(out, "       %s FILE [options] < dataToUpload.bin\n", argv0);
	fprintf(out, "       %s FILE [options] > dumpedData.bin\n\n", argv0);
	fputs("If an input or output file is not specified, the standard input or output will be assumed.\n\n", out);
	fputs("Options:\n", out);
	fputs("-d|--dump\tExtracts and dumps the currently flashed data to a given file or STDOUT.\n", out);
	fputs("-u|--upload\tUploads (flashes) the EEPROM with the supplied data.\n", out);
	fputs("-a|--address\tSpecifies the start address of the EEPROM to start dumping from/uploading to.\n", out);
	fputs("-s|--skip\tWhen uploading, specifies the amount of data to skip from the source file.\n", out);
	fputs("-c|--count\tSpecifies the amount of data to dump/upload.\n", out);
	fputs("-i|--input\tSpecifies the input file to use when uploading.\n", out);
	fputs("-o|--output\tSpecifies the output file to use when dumping.\n", out);
	fputs("-v|--verify\tVerifies data that is written.\n", out);
	fputs("-r|--retain\tRetains any data that lies within blocks of the given data range but that is outside of the data range itself. This is important because the EEPROM gets written in blocks of a certain size, and any unspecified data will be erased.\n", out);
	fputs("-e|--extended\tTreats the EEPROM as an extended 19-bit-addressable device. These devices cannot be written to (yet?). (See README.md for more information.)\n", out);
	fputs("-p|--page\tSelects the page to access on a 4Mbit ROM. See README.me for more.\n", out);
	fputs("-b|--baud\tSet the baud rate to transfer data at. Set to 0 to use the device's current baud rate. The default is 115200.\n", out);
	fputs("--nostty\tDo not set the TTY attributes. This can be useful when the system has trouble setting the baud rate, for example.\n\n", out);

	return 0;
}

int main(int argc, const char* argv[])
{
	uint32_t start = 0;
	uint32_t skip  = 0;
	uint32_t baud  = 115200;
	int32_t count  = -1;
	uint8_t flags  = 0;
	bool    noStty = false;
	uint8_t tempBuffer[4];
	int fdData = 0, fdOut = STDOUT_FILENO;
	const char* inputfilepath = NULL;
	const char* outputfilepath = NULL;
	const char* filePath = argv[1];
	uint8_t data[257];
	bool stderrCanErase;

	if (!isatty(STDOUT_FILENO))
		flags |= FLAG_DOWNLOAD;

	stderrCanErase = isatty(STDERR_FILENO);

	if (argc < 2)
	{
Usage:
		fprintf(stderr, "USAGE: %s FILE [-d|--dump|-u|--upload] [-i|--input=INPUTFILE] [-e|--extended] [-p|--page=PAGE] [-a|--address=START] [-s|--skip=SKIP] [-c|--count=COUNT] [-o|--output=OUTFILE] [-v|--verify] [-r|--retain]\n", argv[0]);
		return 1;
	}

	if (strncmp(argv[1], "--help", 6) == 0 || strncmp(argv[1], "-h", 2) == 0)
		return Usage(argv[0]);


	for (int iArg = 2; iArg < argc; ++iArg)
	{
		if (argv[iArg][0] != '-')
			goto Usage;
		for (int iChar=1; argv[iArg][iChar] != '\0'; ++iChar)
		{
			switch (argv[iArg][iChar])
			{
				case '-':
				{
					const char* argval = strchr(argv[iArg], '=');
					const char* argname = &argv[iArg][2];
					bool inlineValue = argval != NULL; // value is provided within the current argv in `--key=value` format
					argval = inlineValue ? (argval + 1) : argv[iArg + 1];

					if (strncmp(argname, "dump", 4) == 0)
						flags |= FLAG_DOWNLOAD;
					else if (strncmp(argname, "upload", 6) == 0)
						flags &= FLAG_UPLOAD_MASK;
					else if (strncmp(argname, "address", 5) == 0)
					{
						start = strtoul(argval, NULL, 0);
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "skip", 5) == 0)
					{
						skip = strtoul(argval, NULL, 0);
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "count", 5) == 0)
					{
						count = strtol(argval, NULL, 0);
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "input", 5) == 0)
					{
						inputfilepath = argval;
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "output", 5) == 0)
					{
						outputfilepath = argval;
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "verify", 6) == 0)
						flags |= FLAG_VERIFY;
					else if (strncmp(argname, "retain", 6) == 0)
						flags |= FLAG_RETAIN;
					else if (strncmp(argname, "help", 4) == 0)
						return Usage(argv[0]);
					else if (strncmp(argname, "extended", 8) == 0)
						flags |= FLAG_DL_EXTENDED;
					else if (strncmp(argname, "page", 4) == 0)
					{
						if (strtol(argval, NULL, 0) == 0)
							flags &= ~FLAG_DL_EXTPAGE;
						else
							flags |= FLAG_DL_EXTPAGE;
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "baud", 4) == 0)
					{
						baud = strtol(argval, NULL, 0);
						if (!inlineValue) ++iArg;
					}
					else if (strncmp(argname, "nostty", 6) == 0)
						noStty = true;
					goto NextArg;
				} break;
				case 'd': flags |= FLAG_DOWNLOAD; break;
				case 'u': flags &= FLAG_UPLOAD_MASK; break;
				case 'v': flags |= FLAG_VERIFY; break;
				case 'r': flags |= FLAG_RETAIN; break;
				case 'h': return Usage(argv[0]);
				case 'b':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;

					if (argv[iArg][iChar+1] >= 0 && argv[iArg][iChar+1] <= '9')
						baud = strtoul(&argv[iArg][iChar+1], NULL, 0);
					else
						baud = strtoul(argv[++iArg], NULL, 0);
				} goto NextArg;
				case 'e': flags |= FLAG_DL_EXTENDED; break;
				case 'p':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;

					long page;
					if (argv[iArg][iChar+1] >= '0' && argv[iArg][iChar+1] <= '9')
						page = strtol(&argv[iArg][iChar+1], NULL, 0);
					else
						page = strtol(argv[++iArg], NULL, 0);

					if (page == 0) flags &= ~FLAG_DL_EXTPAGE;
					else           flags |=  FLAG_DL_EXTPAGE;
						
				} goto NextArg;
				case 'a':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;
					if (argv[iArg][iChar+1] >= '0' && argv[iArg][iChar+1] <= '9')
						start = strtoul(&argv[iArg][iChar+1], NULL, 0);
					else
						start = strtoul(argv[++iArg], NULL, 0);
				} goto NextArg;
				case 's':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;
					if (argv[iArg][iChar+1] >= '0' && argv[iArg][iChar+1] <= '9')
						skip = strtoul(&argv[iArg][iChar+1], NULL, 0);
					else
						skip = strtoul(argv[++iArg], NULL, 0);
				} goto NextArg;
				case 'i':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;
					if (argv[iArg][iChar+1] > ' ')
						inputfilepath = &argv[iArg][iChar+1];
					else
						inputfilepath = argv[++iArg];
				} goto NextArg;
				case 'o':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;
					if (argv[iArg][iChar+1] > ' ')
						outputfilepath = &argv[iArg][iChar+1];
					else
						outputfilepath = argv[++iArg];
				} goto NextArg;
				case 'c':
				{
					if (argv[iArg][iChar+1] == '=')
						++iChar;
					if (argv[iArg][iChar+1] >= '0' && argv[iArg][iChar+1] <= '9')
						count = strtoul(&argv[iArg][iChar+1], NULL, 0);
					else
						count = strtoul(argv[++iArg], NULL, 0);
				} goto NextArg;
			}
		}
NextArg:
		continue;
	}

	if (inputfilepath != NULL && outputfilepath != NULL)
	{
		fprintf(stderr, "Only an input or output maybe specified, not both.\n");
		return 1;
	}

	if (outputfilepath != NULL)
	{
		flags |= FLAG_DOWNLOAD;
		fdOut = open(outputfilepath, O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	}

	fdProgrammer = open(filePath, O_RDWR | O_SYNC);
	if (fdProgrammer == -1)
	{
		fprintf(stderr, "Error opening programmer: %s\n", strerror(errno));
		return 2;
	}

	if (!noStty)
	{
		struct termios atts;
		if (tcgetattr(fdProgrammer, &atts) != 0)
		{
			fprintf(stderr, "Failed to get attributes for programmer device: %s\n", strerror(errno));
			close(fdProgrammer);
			return 3;
		}
		cfmakeraw(&atts);
		if (baud != 0)
		{
			cfsetispeed(&atts, baud);
			cfsetospeed(&atts, baud);
		}
		atts.c_cc[VTIME] = 20;
		atts.c_cc[VMIN] = 1;
		if (tcsetattr(fdProgrammer, TCSAFLUSH, &atts) != 0)
		{
			fprintf(stderr, "Failed to set attributes for programmer device: %s\n", strerror(errno));
			close(fdProgrammer);
			return 3;
		}
	}
	// MIN == 0
	// TIME > 0

	if ((flags & FLAG_DOWNLOAD) != FLAG_DOWNLOAD)
	{
		if (inputfilepath != NULL)
		{
			fdData = open(inputfilepath, O_RDONLY);
			if (fdData == -1)
			{
				fprintf(stderr, "Error opening data file: %s\n", strerror(errno));
				close(fdProgrammer);
				return 3;
			}
		}
		else
		{
			if (!isatty(STDIN_FILENO))
				fdData = STDIN_FILENO;
			else
			{
				fprintf(stderr, "Please specify an input file, or pipe data into this program.\n");
				close(fdProgrammer);
				return 4;
			}
		}
	}

	tcflush(fdProgrammer, TCIFLUSH);
	fprintf(stderr, "%sloading %d bytes from 0x%06x. Flags = %02Xh\n",
			(flags & FLAG_DOWNLOAD) == FLAG_DOWNLOAD ? "Down" : "Up",
			count,
			start,
			flags);

	tempBuffer[0] = 0xff;
	tempBuffer[1] = flags;
	if (write(fdProgrammer, tempBuffer, 2) != 2)
		fprintf(stderr, "Failed to write control word to programmer\n");
	//fsync(fdProgrammer);
	//tcdrain(fdProgrammer);

	struct TotalState_t TotalState = {.TransferSize = count, 
		                              .StartAddress = start,
									  .BytesTransferred = 0};
	uint32_t address = start;
	sighandler_t oldSignal = signal(SIGINT, &SignalHandler);

	if (skip > 0)
		lseek(fdData, skip, SEEK_CUR);

	while (count > 0)
	{
		tempBuffer[0] = (address >> 24) & 0xFF;
		tempBuffer[1] = (address >> 16) & 0xFF;
		tempBuffer[2] = (address >>  8) & 0xFF;
		tempBuffer[3] = (address >>  0) & 0xFF;
		if (write(fdProgrammer, tempBuffer, 4) != 4)
			fprintf(stderr, "Failed to write address word to programmer\n");
		//fsync(fdProgrammer);
		//tcdrain(fdProgrammer);

		unsigned int nBytesRemainingInFifo = 0;
		int thisCount;
		if ((flags & FLAG_DOWNLOAD) != FLAG_DOWNLOAD)
		{
			thisCount = count > 256 ? 0xFF : (count-1);
			if ((address & 0xFF) != 0)
			{
				int lengthUntilNextSector = (0x100 - (address & 0xFF)) - 1;
				if (lengthUntilNextSector < thisCount)
					thisCount = lengthUntilNextSector;
			}
			count -= thisCount + 1;
			if (write(fdProgrammer, &thisCount, 1) != 1)
			{
				fprintf(stderr, "Error writing to programmer\n");
				goto Terminate;
			}

			if (read(fdData, data, thisCount+1) != thisCount+1)
			{
				fprintf(stderr, "Error reading data from source file\n");
				goto Terminate;
			}

			if (write(fdProgrammer, data, thisCount+1) != thisCount+1)
			{
				fprintf(stderr, "Error writing to programmer\n");
				goto Terminate;
			}

			if (read(fdProgrammer, tempBuffer, 1) != 1)
			{
				fprintf(stderr, "Error reading from programmer\n");
				goto Terminate;
			}

			TotalState.BytesTransferred += thisCount+1;
			if (stderrCanErase)
				fprintf(stderr, "\033[1K\033[1G%u/%u bytes transferred", TotalState.BytesTransferred, TotalState.TransferSize);
			else
				fprintf(stderr, ".");

			if (tempBuffer[0] != 0)
			{
				if (tempBuffer[0] == 2)
				{
					// programming error
					if (!ReadAll(fdProgrammer, &tempBuffer[1], 2, NULL, NULL))
					{
						fprintf(stderr, "Error reading from programmer\n");
						goto Terminate;
					}
					fprintf(stderr,
							"Programming error. Last byte of sector was %02Xh, but came back as %02Xh\n",
							tempBuffer[1],
							tempBuffer[2]);
				}
				else if (tempBuffer[0] == 1)
				{
					if (!ReadAll(fdProgrammer, &tempBuffer[0], 1, NULL, NULL))
					{
						fprintf(stderr, "Error reading from programmer\n");
						goto Terminate;
					}
					for (int i=0; i<=tempBuffer[0]; ++i)
					{
						if (!ReadAll(fdProgrammer, &tempBuffer[1], 2, NULL, NULL))
						{
							fprintf(stderr, "Error reading from programmer\n");
							goto Terminate;
						}
						fprintf(stderr, 
								"Verification error at 0x%06x: Expected %02Xh, got %02Xh\n",
								((start + i) & 0xFF) | (start & ~0xFF),
								data[i],
								tempBuffer[2]);
					}
				}
			}
		}
		else
		{
			thisCount = count > 256 ? 0xFF : (count-1);
			count -= thisCount + 1;
			if (ioctl(fdProgrammer, FIONREAD, &nBytesRemainingInFifo) == 0 && nBytesRemainingInFifo != 0)
			{
				fprintf(stderr, "%u bytes discarded\n", nBytesRemainingInFifo);
				tcflush(fdProgrammer, TCIFLUSH);
			}
			if (write(fdProgrammer, &thisCount, 1) != 1)
			{
				fprintf(stderr, "Error writing to programmer\n");
				goto Terminate;
			}

			if (!ReadAll(fdProgrammer, data, thisCount+2, &RxCallback, &TotalState))
			{
				fprintf(stderr, "Error reading from programmer\n");
				goto Terminate;
			}
			if (data[thisCount+1] != 0)
			{
				fprintf(stderr, "Invalid data received in sector %u (address starting %03xh)\n",
						address >> 8,
						address & ~0xFF);
				if (ioctl(fdProgrammer, FIONREAD, &nBytesRemainingInFifo) == 0)
					fprintf(stderr, "%u bytes remaining in FIFO\n", nBytesRemainingInFifo);
				goto Terminate;
			}
			TotalState.BytesTransferred += thisCount+1;
			if (write(fdOut, data, thisCount+1) != thisCount+1)
			{
				fprintf(stderr, "Error writing to programmer\n");
				goto Terminate;
			}
			fsync(fdOut);
		}
		address += thisCount+1;
	}
	signal(SIGINT, oldSignal);

Terminate:
	if (fdData > 0 && fdData != STDIN_FILENO)
		close(fdData);

	if (!isatty(fdOut))
	{
		fsync(fdOut);
		close(fdOut);
	}

	if (fdProgrammer >= 0)
		close(fdProgrammer);

	return 0;
}
