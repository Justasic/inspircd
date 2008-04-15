/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2008 InspIRCd Development Team
 * See: http://www.inspircd.org/wiki/index.php/Credits
 *
 * This program is free but copyrighted software; see
 *	    the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#ifndef WINDOWS
#include <sys/select.h>
#endif // WINDOWS
#include "socketengines/socketengine_select.h"


SelectEngine::SelectEngine(InspIRCd* Instance) : SocketEngine(Instance)
{
	MAX_DESCRIPTORS = FD_SETSIZE;
	EngineHandle = 0;
	CurrentSetSize = 0;

	writeable = new bool [GetMaxFds()];
	memset(writeable, 0, sizeof(writeable));
	ref = new EventHandler* [GetMaxFds()];
	memset(ref, 0, GetMaxFds() * sizeof(EventHandler*));
	ev = new EventHandler* [GetMaxFds()];
}

SelectEngine::~SelectEngine()
{
	delete[] ref;
	delete[] ev;
}

bool SelectEngine::AddFd(EventHandler* eh)
{
	int fd = eh->GetFd();
	if ((fd < 0) || (fd > GetMaxFds() - 1))
		return false;

	if (GetRemainingFds() <= 1)
		return false;

	if (ref[fd])
		return false;

	fds[fd] = fd;
	ref[fd] = eh;
	CurrentSetSize++;

	ServerInstance->Logs->Log("SOCKET",DEBUG,"New file descriptor: %d", fd);
	return true;
}

void SelectEngine::WantWrite(EventHandler* eh)
{
	writeable[eh->GetFd()] = true;
}

bool SelectEngine::DelFd(EventHandler* eh, bool force)
{
	int fd = eh->GetFd();

	if ((fd < 0) || (fd > GetMaxFds() - 1))
		return false;

	std::map<int,int>::iterator t = fds.find(fd);
	if (t != fds.end())
		fds.erase(t);

	CurrentSetSize--;
	ref[fd] = NULL;

	ServerInstance->Logs->Log("SOCKET",DEBUG,"Remove file descriptor: %d", fd);
	return true;
}

int SelectEngine::GetMaxFds()
{
	return FD_SETSIZE;
}

int SelectEngine::GetRemainingFds()
{
	return GetMaxFds() - CurrentSetSize;
}

int SelectEngine::DispatchEvents()
{
	timeval tval;
	int sresult = 0;
	socklen_t codesize;
	int errcode = 0;

	FD_ZERO(&wfdset);
	FD_ZERO(&rfdset);
	FD_ZERO(&errfdset);

	/* Populate the select FD set (this is why select sucks compared to epoll, kqueue, IOCP) */
	for (std::map<int,int>::iterator a = fds.begin(); a != fds.end(); a++)
	{
		if (ref[a->second]->Readable())
			/* Read notifications */
			FD_SET (a->second, &rfdset);
		else
			/* Write notifications */
			FD_SET (a->second, &wfdset);

		/* Explicitly one-time writeable */
		if (writeable[a->second])
			FD_SET (a->second, &wfdset);

		/* All sockets must receive error notifications regardless */
		FD_SET (a->second, &errfdset);
	}

	/* One second waits */
	tval.tv_sec = 1;
	tval.tv_usec = 0;

	sresult = select(FD_SETSIZE, &rfdset, &wfdset, &errfdset, &tval);

	/* Nothing to process this time around */
	if (sresult < 1)
		return 0;

	/* Safe assumption (as of 1.1 anyway) that a socket can't remove itself from the list in the middle of the loop */
	for (std::map<int,int>::iterator a = fds.begin(); a != fds.end(); a++)
	{
		EventHandler* ev = ref[a->second];
		if (ev)
		{
			if (FD_ISSET (ev->GetFd(), &errfdset))
			{
				ErrorEvents++;
				if (getsockopt(ev->GetFd(), SOL_SOCKET, SO_ERROR, (char*)&errcode, &codesize) < 0)
					errcode = errno;

				ev->HandleEvent(EVENT_ERROR, errcode);
				continue;
			}
			else
			{
				/* NOTE: This is a pair of seperate if statements as the socket
				 * may be in both read and writeable state at the same time.
				 * If an error event occurs above it is not worth processing the
				 * read and write states even if set.
				 */
				if (FD_ISSET (ev->GetFd(), &wfdset))
				{
					WriteEvents++;
					writeable[ev->GetFd()] = false;
					ev->HandleEvent(EVENT_WRITE);
				}
				if (FD_ISSET (ev->GetFd(), &rfdset))
				{
						ReadEvents++;
						ev->HandleEvent(EVENT_READ);
				}
			}
		}
	}

	return result;
}

std::string SelectEngine::GetName()
{
	return "select";
}
