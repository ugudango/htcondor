/***************************************************************
 *
 * Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/


#include "condor_common.h"
#include "condor_classad.h"
#include "condor_attributes.h"
#include "condor_debug.h"
#include "condor_io.h"
#include "condor_uid.h"
#include "shadow.h"
#include "pseudo_ops.h"
#include "condor_config.h"
#include "exit.h"
#include "filename_tools.h"
#include "basename.h"
#include "nullfile.h"

extern ReliSock *syscall_sock;
extern BaseShadow *Shadow;
extern RemoteResource *thisRemoteResource;
extern RemoteResource *parallelMasterResource;

static void append_buffer_info( std::string &url, const char *method, char const *path );
static int use_append( const char *method, const char *path );
static int use_compress( const char *method, const char *path );
static int use_fetch( const char *method, const char *path );
static int use_local_access( const char *file );

int
pseudo_register_machine_info(char * /* uiddomain */, char * /* fsdomain */, 
							 char * starterAddr, char *full_hostname )
{

	thisRemoteResource->setStarterAddress( starterAddr );
	thisRemoteResource->setMachineName( full_hostname );

		/* For backwards compatibility, if we get this old pseudo call
		   from the starter, we assume we're not going to get the
		   happy new pseudo_begin_execution call.  So, pretend we got
		   it now so we still log execute events and so on, even if
		   it's not as acurate as we'd like.
		*/
	thisRemoteResource->beginExecution();
	return 0;
}


int
pseudo_register_starter_info( ClassAd* ad )
{
	thisRemoteResource->setStarterInfo( ad );
	return 0;
}


int
pseudo_register_job_info(ClassAd* ad)
{
	// The starter sends an update with provisioned resources but no
	// JobState atrribute before the job begins execution.
	// Updates with a JobState attribute occur after the job is running.
	if (thisRemoteResource->getResourceState() == RR_STARTUP && ad->Lookup(ATTR_JOB_STATE)) {
		// We missed a begin_execution syscall, probably due to a disconnect.
		// Do the begin_execution logic now.
		dprintf(D_FULLDEBUG, "Received a register_job_info syscall without a begin_execution syscall. Doing begin_execution logic.\n");
		thisRemoteResource->beginExecution();
	}

	fix_update_ad(*ad);
	Shadow->updateFromStarterClassAd(ad);
	return 0;
}


int
pseudo_begin_execution()
{
	thisRemoteResource->beginExecution();
	return 0;
}


// In rare instances, the ad this function returns will need to be
// deleted by the caller. In those cases, delete_ad will be set to true.
// Otherwise, delete_ad will be set to false and the returned ad should
// not be deleted.
int
pseudo_get_job_info(ClassAd *&ad, bool &delete_ad)
{
	ClassAd * the_ad;
	delete_ad = false;

	the_ad = thisRemoteResource->getJobAd();
	ASSERT( the_ad );

	thisRemoteResource->initFileTransfer();

	Shadow->publishShadowAttrs( the_ad );

	ad = the_ad;

	return 0;
}


int
pseudo_get_user_info(ClassAd *&ad)
{
	static ClassAd* user_ad = NULL;


	if( ! user_ad ) {
			// if we don't have the ClassAd yet, allocate it and fill
			// it in with the info we care about
		user_ad = new ClassAd;

#ifndef WIN32
		user_ad->Assign( ATTR_UID, (int)get_user_uid() );

		user_ad->Assign( ATTR_GID, (int)get_user_gid() );
#endif

	}

	ad = user_ad;
	return 0;
}

// The list of attributes that some (old?) statrers try and incorrectly update
// this table is used to remove or rename them before we process the update ad.
//
typedef struct {
	const char * const updateAttr; // name in update ad
	const char * const newAttr;    // rename to this before processing the update ad, if NULL, delete the attribute
} AttrToAttr;
static const AttrToAttr updateAdBlacklist[] = {
	// Prior to 8.7.2, the starter incorrectly sends JobStartDate each time the job starts
	// but JobStartDate is defined to be the timestamp of the FIRST execution of the job
	// and it is set by the Schedd the first time it makes a shadow, so we want to just delete
	// this attribute.
	{ ATTR_JOB_START_DATE, NULL },
};

void fix_update_ad(ClassAd & update_ad)
{
	// remove or rename attributes in the update ad before we process it.
	for (size_t ii = 0; ii < COUNTOF(updateAdBlacklist); ++ii) {
		ExprTree * tree = update_ad.Remove(updateAdBlacklist[ii].updateAttr);
		if (tree) {
			if (IsDebugLevel(D_MACHINE)) {
				dprintf(D_MACHINE, "Update ad contained '%s=%s' %s it\n",
					updateAdBlacklist[ii].updateAttr, ExprTreeToString(tree),
					updateAdBlacklist[ii].newAttr ? "Renaming" : "Removing"
					);
			}
			if (updateAdBlacklist[ii].newAttr) {
				update_ad.Insert(updateAdBlacklist[ii].newAttr, tree);
			} else {
				delete tree;
			}
		}
	}
}

int
pseudo_job_exit(int status, int reason, ClassAd* ad)
{
	if (thisRemoteResource->getResourceState() == RR_STARTUP) {
		// We missed a begin_execution syscall, probably due to a disconnect.
		// Do the begin_execution logic now.
		dprintf(D_FULLDEBUG, "Received a job_exit syscall without a begin_execution syscall. Doing begin_execution logic.\n");
		thisRemoteResource->beginExecution();
	}

	// reset the reason if less than EXIT_CODE_OFFSET so that
	// an older starter can be made compatible with the newer
	// schedd exit reasons.
	if ( reason < EXIT_CODE_OFFSET ) {
		if ( reason != JOB_EXCEPTION && reason != DPRINTF_ERROR ) {
			reason += EXIT_CODE_OFFSET;
			dprintf(D_SYSCALLS, "in pseudo_job_exit: old starter, reason reset"
				" from %d to %d\n",reason-EXIT_CODE_OFFSET,reason);
		}
	}
	dprintf(D_SYSCALLS, "in pseudo_job_exit: status=%d,reason=%d\n",
			status, reason);

	// Despite what exit.h says, JOB_COREDUMPED is set by the starter only
	// when the job is NOT killed (and left a core file).
	if( reason == JOB_EXITED
		|| reason == JOB_EXITED_AND_CLAIM_CLOSING
		|| reason == JOB_COREDUMPED ) {
		thisRemoteResource->incrementJobCompletionCount();
	}
	fix_update_ad(*ad);
	thisRemoteResource->updateFromStarter( ad );
	thisRemoteResource->resourceExit( reason, status );
	Shadow->updateJobInQueue( U_STATUS );
	return 0;
}

int 
pseudo_job_termination( ClassAd *ad )
{
	bool exited_by_signal = false;
	bool core_dumped = false;
	int exit_signal = 0;
	int exit_code = 0;
	std::string exit_reason;

	if (thisRemoteResource->getResourceState() == RR_STARTUP) {
		// We missed a begin_execution syscall, probably due to a disconnect.
		// Do the begin_execution logic now.
		dprintf(D_FULLDEBUG, "Received a job_termination syscall without a begin_execution syscall. Doing begin_execution logic.\n");
		thisRemoteResource->beginExecution();
	}

	ad->LookupBool(ATTR_ON_EXIT_BY_SIGNAL,exited_by_signal);
	ad->LookupBool(ATTR_JOB_CORE_DUMPED,core_dumped);
	ad->LookupString(ATTR_EXIT_REASON,exit_reason);

	// Only one of these next two exist.
	ad->LookupInteger(ATTR_ON_EXIT_SIGNAL,exit_signal);
	ad->LookupInteger(ATTR_ON_EXIT_CODE,exit_code);

	// This will utilize only the correct arguments depending on if the
	// process exited with a signal or not.
	Shadow->mockTerminateJob( exit_reason, exited_by_signal, exit_code,
		exit_signal, core_dumped );

	return 0;
}


int
pseudo_register_mpi_master_info( ClassAd* ad ) 
{
	char *addr = NULL;

	if( ! ad->LookupString(ATTR_MPI_MASTER_ADDR, &addr) ) {
		dprintf( D_ALWAYS,
				 "ERROR: mpi_master_info ClassAd doesn't contain %s\n",
				 ATTR_MPI_MASTER_ADDR );
		return -1;
	}
	if( ! Shadow->setMpiMasterInfo(addr) ) {
		dprintf( D_ALWAYS, "ERROR: received "
				 "pseudo_register_mpi_master_info for a non-MPI job!\n" );
		free(addr);
		return -1;
	}
	free(addr);
	return 0;
}

/*
If short_path is an absolute path, copy it to full path.
Otherwise, tack the current directory on to the front
of short_path, and copy it to full_path.
Notice that the old shadow kept track of the job as it
moved around, but there is no such notion in this shadow,
so CurrentWorkingDir is replaced with the job's iwd.
*/
 
static void complete_path( const char *short_path, std::string &full_path )
{
	if ( fullpath(short_path) ) {
		full_path = short_path;
	} else {
		formatstr(full_path, "%s%s%s",
						  Shadow->getIwd(),
						  DIR_DELIM_STRING,
						  short_path);
	}
}

/*
This call translates a logical path name specified by a user job
into an actual url which describes how and where to fetch
the file from.  For example, joe/data might become buffer:remote:/usr/joe/data
*/

int pseudo_get_file_info_new( const char *logical_name, char *&actual_url )
{
	std::string remap_list;
	std::string split_dir;
	std::string split_file;
	std::string full_path;
	std::string remap;
	std::string urlbuf;
	const char	*method = NULL;

	dprintf( D_SYSCALLS, "\tlogical_name = \"%s\"\n", logical_name );

	ASSERT( actual_url == NULL );

	/* The incoming logical name might be a simple, relative, or complete path */
	/* We need to examine both the full path and the simple name. */

	filename_split( logical_name, split_dir, split_file );
	complete_path( logical_name, full_path );

	/* Any name comparisons must check the logical name, the simple name, and the full path */

	if(Shadow->getJobAd()->LookupString(ATTR_FILE_REMAPS,remap_list) &&
	  (filename_remap_find( remap_list.c_str(), logical_name, remap ) ||
	   filename_remap_find( remap_list.c_str(), split_file.c_str(), remap ) ||
	   filename_remap_find( remap_list.c_str(), full_path.c_str(), remap ))) {

		dprintf(D_SYSCALLS,"\tremapped to: %s\n",remap.c_str());

		/* If the remap is a full URL, return right away */
		/* Otherwise, continue processing */

		if(strchr(remap.c_str(),':')) {
			dprintf(D_SYSCALLS,"\tremap is complete url\n");
			actual_url = strdup(remap.c_str());
			return 0;
		} else {
			dprintf(D_SYSCALLS,"\tremap is simple file\n");
			complete_path( remap.c_str(), full_path );
		}
	} else {
		dprintf(D_SYSCALLS,"\tnot remapped\n");
	}

	dprintf( D_SYSCALLS,"\tfull_path = \"%s\"\n", full_path.c_str() );

	/* Now, we have a full pathname. */
	/* Figure out what url modifiers to slap on it. */

	if( use_local_access(full_path.c_str()) ) {
		method = "local";
	} else {
		method = "remote";
	}

	if( use_fetch(method,full_path.c_str()) ) {
		urlbuf += "fetch:";
	}

	if( use_compress(method,full_path.c_str()) ) {
		urlbuf += "compress:";
	}

	append_buffer_info(urlbuf,method,full_path.c_str());

	if( use_append(method,full_path.c_str()) ) {
		urlbuf += "append:";
	}

	if (method) {
		urlbuf += method;
		urlbuf += ":";
	}
	urlbuf += full_path;
	actual_url = strdup(urlbuf.c_str());

	dprintf(D_SYSCALLS,"\tactual_url: %s\n",actual_url);

	return 0;
}

static void append_buffer_info( std::string &url, const char *method, char const *path )
{
	std::string buffer_list;
	std::string buffer_string;
	std::string dir;
	std::string file;
	int s,bs,ps;
	int result;

	filename_split(path,dir,file);

	/* Do not buffer special device files, whether local or remote */
	if(!strncmp(path,"/dev/",5)) return;

	/* Get the default buffer setting */
	pseudo_get_buffer_info( &s, &bs, &ps );

	/* Now check for individual file overrides */
	/* These lines have the same syntax as a remap list */

	if(Shadow->getJobAd()->LookupString(ATTR_BUFFER_FILES,buffer_list)) {
		if( filename_remap_find(buffer_list.c_str(),path,buffer_string) ||
		    filename_remap_find(buffer_list.c_str(),file.c_str(),buffer_string) ) {

			/* If the file is merely mentioned, turn on the default buffer */
			url += "buffer:";

			/* If there is also a size setting, use that */
			result = sscanf(buffer_string.c_str(),"(%d,%d)",&s,&bs);
			if( result==2 ) url += buffer_string;

			return;
		}
	}

	/* Turn on buffering if the value is set and is not special or local */
	/* In this case, use the simple syntax 'buffer:' so as not to confuse old libs */

	if (s>0 && bs>0 && method && strcmp(method,"local")) {
		url += "buffer:";
	}
}

/* Return true if this JobAd attribute contains this path */

static int attr_list_has_file( const char *attr, const char *path )
{
	char const *file;
	std::string str;

	file = condor_basename(path);

	Shadow->getJobAd()->LookupString(attr,str);
	StringList list(str.c_str());

	if( list.contains_withwildcard(path) || list.contains_withwildcard(file) ) {
		return 1;
	} else {
		return 0;
	}
}

static int use_append( const char * /* method */, const char *path )
{
	return attr_list_has_file( ATTR_APPEND_FILES, path );
}

static int use_compress( const char * /* method */, const char *path )
{
	return attr_list_has_file( ATTR_COMPRESS_FILES, path );
}

static int use_fetch( const char * /* method */, const char *path )
{
	return attr_list_has_file( ATTR_FETCH_FILES, path );
}

/*
Return the buffer configuration.  If the classad contains nothing,
assume it is zero.
*/

int pseudo_get_buffer_info( int *bytes_out, int *block_size_out, int *prefetch_bytes_out )
{
	int bytes=0, block_size=0;

	Shadow->getJobAd()->LookupInteger(ATTR_BUFFER_SIZE,bytes);
	Shadow->getJobAd()->LookupInteger(ATTR_BUFFER_BLOCK_SIZE,block_size);

	if( bytes<0 ) bytes = 0;
	if( block_size<0 ) block_size = 0;
	if( bytes<block_size ) block_size = bytes;

	*bytes_out = bytes;
	*block_size_out = block_size;
	*prefetch_bytes_out = 0;

	dprintf(D_SYSCALLS,"\tbuffer configuration is bytes=%d block_size=%d\n",bytes, block_size );

	return 0;
}

static int use_local_access( const char *file )
{
	return
		!strcmp(file,"/dev/null") ||
		!strcmp(file,"/dev/zero") ||
		attr_list_has_file( ATTR_LOCAL_FILES, file );
}

int
pseudo_ulog( ClassAd *ad )
{
	// Ignore the event time we were given, use the
	// current time and timezone
	ad->Delete("EventTime");

	ULogEvent *event = instantiateEvent(ad);
	int result = 0;
	char const *critical_error = NULL;
	std::string CriticalErrorBuf;
	bool event_already_logged = false;
	bool put_job_on_hold = false;
	std::string hold_reason;
	int hold_reason_code = 0;
	int hold_reason_sub_code = 0;

	if(!event) {
		std::string add_str;
		sPrintAd(add_str, *ad);
		dprintf(
		  D_ALWAYS,
		  "invalid event ClassAd in pseudo_ulog: %s\n",
		  add_str.c_str());
		return -1;
	}

	if(ad->LookupInteger(ATTR_HOLD_REASON_CODE,hold_reason_code) &&
		hold_reason_code > 0)
	{
		put_job_on_hold = true;
		ad->LookupInteger(ATTR_HOLD_REASON_SUBCODE,hold_reason_sub_code);
		ad->LookupString(ATTR_HOLD_REASON,hold_reason);
	}

	if( event->eventNumber == ULOG_REMOTE_ERROR ) {
		RemoteErrorEvent *err = (RemoteErrorEvent *)event;

		if(!err->getExecuteHost() || !*err->getExecuteHost()) {
			//Insert remote host information.
			char *execute_host = NULL;
			thisRemoteResource->getMachineName(execute_host);
			err->execute_host = execute_host;
			free(execute_host);
		}

		if(err->isCriticalError()) {
			formatstr(
			  CriticalErrorBuf,
			  "Error from %s: %s",
			  err->getExecuteHost(),
			  err->getErrorText());

			critical_error = CriticalErrorBuf.c_str();
			if(hold_reason.empty()) {
				hold_reason = critical_error;
			}

			//Temporary: the following causes critical remote errors
			//to be logged as ShadowExceptionEvents, rather than
			//RemoteErrorEvents.  The result is ugly, but guaranteed to
			//be compatible with other user-log reading tools.
			BaseShadow::log_except(critical_error);
			event_already_logged = true;
		}
	}

	if( !event_already_logged && !Shadow->uLog.writeEvent( event, ad ) ) {
		std::string add_str;
		sPrintAd(add_str, *ad);
		dprintf(
		  D_ALWAYS,
		  "unable to log event in pseudo_ulog: %s\n",
		  add_str.c_str());
		result = -1;
	}

	if(put_job_on_hold) {
		if (critical_error) hold_reason = critical_error;
		if(hold_reason.empty()) {
			hold_reason = "Job put on hold by remote host.";
		}
		// Let the RemoteResource know that the starter is shutting
		// down and failing to kill it it expected.
		thisRemoteResource->resourceExit( JOB_SHOULD_HOLD, -1 );
		Shadow->holdJobAndExit(hold_reason.c_str(),hold_reason_code,hold_reason_sub_code);
		//should never get here, because holdJobAndExit() exits.
	}

	if( critical_error ) {
		//Suppress ugly "Shadow exception!"
		Shadow->exception_already_logged = true;

		//lame: at the time of this writing, EXCEPT does not want const:
		EXCEPT("%s", critical_error);
	}

	delete event;
	return result;
}

int
pseudo_get_job_ad( ClassAd* &ad )
{
	RemoteResource *remote;
	if (parallelMasterResource == NULL) {
		remote = thisRemoteResource;
	} else {
		remote = parallelMasterResource;
	}
	ad = remote->getJobAd();
	return 0;
}


int
pseudo_get_job_attr( const char *name, std::string &expr )
{
	RemoteResource *remote;
	if (parallelMasterResource == NULL) {
		remote = thisRemoteResource;
	} else {
		remote = parallelMasterResource;
	}
	ClassAd *ad = remote->getJobAd();

	ExprTree *e = ad->LookupExpr(name);
	if(e) {
		expr = ExprTreeToString(e);
		dprintf(D_SYSCALLS,"pseudo_get_job_attr(%s) = %s\n",name,expr.c_str());
		return 0;
	} else {
		dprintf(D_SYSCALLS,"pseudo_get_job_attr(%s) is UNDEFINED\n",name);
		expr = "UNDEFINED";
		return 0;
	}
}

int
pseudo_set_job_attr( const char *name, const char *expr, bool log )
{
	RemoteResource *remote;
	if (parallelMasterResource == NULL) {
		remote = thisRemoteResource;
	} else {
		remote = parallelMasterResource;
	}
	if(Shadow->updateJobAttr(name,expr,log)) {
		dprintf(D_SYSCALLS,"pseudo_set_job_attr(%s,%s) succeeded\n",name,expr);
		ClassAd *ad = remote->getJobAd();
		ASSERT(ad);
		ad->AssignExpr(name,expr);
		return 0;
	} else {
		dprintf(D_SYSCALLS,"pseudo_set_job_attr(%s,%s) failed\n",name,expr);
		return -1;
	}
}

int
pseudo_constrain( const char *expr )
{
	std::string reqs;
	std::string newreqs;

	dprintf(D_SYSCALLS,"pseudo_constrain(%s)\n",expr);
	dprintf(D_SYSCALLS,"\tchanging AgentRequirements to %s\n",expr);
	
	if(pseudo_set_job_attr("AgentRequirements",expr)!=0) return -1;
	if(pseudo_get_job_attr("Requirements",reqs)!=0) return -1;

	if(strstr(reqs.c_str(),"AgentRequirements")) {
		dprintf(D_SYSCALLS,"\tRequirements already refers to AgentRequirements\n");
		return 0;
	} else {
		formatstr(newreqs, "(%s) && AgentRequirements", reqs.c_str());
		dprintf(D_SYSCALLS,"\tchanging Requirements to %s\n",newreqs.c_str());
		return pseudo_set_job_attr("Requirements",newreqs.c_str());
	}
}

int pseudo_get_sec_session_info(
	char const *starter_reconnect_session_info,
	std::string &reconnect_session_id,
	std::string &reconnect_session_info,
	std::string &reconnect_session_key,
	char const *starter_filetrans_session_info,
	std::string &filetrans_session_id,
	std::string &filetrans_session_info,
	std::string &filetrans_session_key)
{
	RemoteResource *remote;
	if (parallelMasterResource == NULL) {
		remote = thisRemoteResource;
	} else {
		remote = parallelMasterResource;
	}

	bool rc = remote->getSecSessionInfo(
		starter_reconnect_session_info,
		reconnect_session_id,
		reconnect_session_info,
		reconnect_session_key,
		starter_filetrans_session_info,
		filetrans_session_id,
		filetrans_session_info,
		filetrans_session_key);

	if( !rc ) {
		return -1;
	}
	return 1;
}

//
// This syscall MUST ignore information it doesn't know how to deal with.
//
// The thinking for this syscall is the following: as Miron asks for more
// metrics, it's highly likely that there will be other event notifications
// that the starter needs to send to the shadow.  Rather than create a
// semantically significant remote syscall for each one, let's just create
// a single one that they can all use.  (As a new syscall, pools won't see
// correct values for the new metrics until both the shadow and the starter
// have been upgraded.)  However, rather than writing a bunch of complicated
// code to determine which version(s) can send what events to which
// version(s), we can just declare and require that this function just
// ignore attributes it doesn't know how to deal with (and not fail if an
// attribute has the wrong type).
//

int
pseudo_event_notification( const ClassAd & ad ) {
	std::string eventType;
	if(! ad.LookupString( "EventType", eventType )) {
		return 0;
	}

	ClassAd * jobAd = Shadow->getJobAd();
	ASSERT(jobAd);

	if( eventType == "ActivationExecutionExit" ) {
		thisRemoteResource->recordActivationExitExecutionTime(time(NULL));
	}

	return 0;
}
