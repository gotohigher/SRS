/*
The MIT License (MIT)

Copyright (c) 2013 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_core_encoder.hpp>

#include <stdlib.h>
#include <unistd.h>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>
#include <srs_core_config.hpp>

#define SRS_ENCODER_SLEEP_MS 2000

#define SRS_ENCODER_VCODEC "libx264"
#define SRS_ENCODER_ACODEC "libaacplus"

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
	started			= false;
	pid				= -1;
	ffmpeg 			= ffmpeg_bin;
	
	vbitrate 		= 0;
	vfps 			= 0;
	vwidth 			= 0;
	vheight 		= 0;
	vthreads 		= 0;
	abitrate 		= 0;
	asample_rate 	= 0;
	achannels 		= 0;
}

SrsFFMPEG::~SrsFFMPEG()
{
	stop();
}

int SrsFFMPEG::initialize(std::string vhost, std::string port, std::string app, std::string stream, SrsConfDirective* engine)
{
	int ret = ERROR_SUCCESS;
	
	config->get_engine_vfilter(engine, vfilter);
	vcodec 			= config->get_engine_vcodec(engine);
	vbitrate 		= config->get_engine_vbitrate(engine);
	vfps 			= config->get_engine_vfps(engine);
	vwidth 			= config->get_engine_vwidth(engine);
	vheight 		= config->get_engine_vheight(engine);
	vthreads 		= config->get_engine_vthreads(engine);
	vprofile 		= config->get_engine_vprofile(engine);
	vpreset 		= config->get_engine_vpreset(engine);
	config->get_engine_vparams(engine, vparams);
	acodec 			= config->get_engine_acodec(engine);
	abitrate 		= config->get_engine_abitrate(engine);
	asample_rate 	= config->get_engine_asample_rate(engine);
	achannels 		= config->get_engine_achannels(engine);
	config->get_engine_aparams(engine, aparams);
	output 			= config->get_engine_output(engine);
	
	// ensure the size is even.
	vwidth -= vwidth % 2;
	vheight -= vheight % 2;

	// input stream, from local.
	// ie. rtmp://127.0.0.1:1935/live/livestream
	input = "rtmp://127.0.0.1:";
	input += port;
	input += "/";
	input += app;
	input += "/";
	input += stream;
	
	// output stream, to other/self server
	// ie. rtmp://127.0.0.1:1935/live/livestream_sd
	if (vhost == RTMP_VHOST_DEFAULT) {
		output = srs_replace(output, "[vhost]", "127.0.0.1");
	} else {
		output = srs_replace(output, "[vhost]", vhost);
	}
	output = srs_replace(output, "[port]", port);
	output = srs_replace(output, "[app]", app);
	output = srs_replace(output, "[stream]", stream);

	// important: loop check, donot transcode again.
	// we think the following is loop circle:
	// input: rtmp://127.0.0.1:1935/live/livestream_sd
	// output: rtmp://127.0.0.1:1935/live/livestream_sd_sd
	std::string tail = ""; // tail="_sd"
	if (output.length() > input.length()) {
		tail = output.substr(input.length());
	}
	// TODO: better dead loop check.
	// if input also endwiths the tail, loop detected.
	if (!tail.empty() && input.rfind(tail) == input.length() - tail.length()) {
		ret = ERROR_ENCODER_LOOP;
		srs_info("detect a loop cycle, input=%s, output=%s, ignore it. ret=%d",
			input.c_str(), output.c_str(), ret);
		return ret;
	}
	
	if (vcodec != SRS_ENCODER_VCODEC) {
		ret = ERROR_ENCODER_VCODEC;
		srs_error("invalid vcodec, must be %s, actual %s, ret=%d",
			SRS_ENCODER_VCODEC, vcodec.c_str(), ret);
		return ret;
	}
	if (vbitrate <= 0) {
		ret = ERROR_ENCODER_VBITRATE;
		srs_error("invalid vbitrate: %d, ret=%d", vbitrate, ret);
		return ret;
	}
	if (vfps <= 0) {
		ret = ERROR_ENCODER_VFPS;
		srs_error("invalid vfps: %.2f, ret=%d", vfps, ret);
		return ret;
	}
	if (vwidth <= 0) {
		ret = ERROR_ENCODER_VWIDTH;
		srs_error("invalid vwidth: %d, ret=%d", vwidth, ret);
		return ret;
	}
	if (vheight <= 0) {
		ret = ERROR_ENCODER_VHEIGHT;
		srs_error("invalid vheight: %d, ret=%d", vheight, ret);
		return ret;
	}
	if (vthreads < 0) {
		ret = ERROR_ENCODER_VTHREADS;
		srs_error("invalid vthreads: %d, ret=%d", vthreads, ret);
		return ret;
	}
	if (vprofile.empty()) {
		ret = ERROR_ENCODER_VPROFILE;
		srs_error("invalid vprofile: %s, ret=%d", vprofile.c_str(), ret);
		return ret;
	}
	if (vpreset.empty()) {
		ret = ERROR_ENCODER_VPRESET;
		srs_error("invalid vpreset: %s, ret=%d", vpreset.c_str(), ret);
		return ret;
	}
	if (acodec != SRS_ENCODER_ACODEC) {
		ret = ERROR_ENCODER_ACODEC;
		srs_error("invalid acodec, must be %s, actual %s, ret=%d",
			SRS_ENCODER_ACODEC, acodec.c_str(), ret);
		return ret;
	}
	if (abitrate <= 0) {
		ret = ERROR_ENCODER_ABITRATE;
		srs_error("invalid abitrate: %d, ret=%d", 
			abitrate, ret);
		return ret;
	}
	if (asample_rate <= 0) {
		ret = ERROR_ENCODER_ASAMPLE_RATE;
		srs_error("invalid sample rate: %d, ret=%d", 
			asample_rate, ret);
		return ret;
	}
	if (achannels != 1 && achannels != 2) {
		ret = ERROR_ENCODER_ACHANNELS;
		srs_error("invalid achannels, must be 1 or 2, actual %d, ret=%d", 
			achannels, ret);
		return ret;
	}
	if (output.empty()) {
		ret = ERROR_ENCODER_OUTPUT;
		srs_error("invalid empty output, ret=%d", ret);
		return ret;
	}
	
	return ret;
}

int SrsFFMPEG::start()
{
	int ret = ERROR_SUCCESS;
	
	if (started) {
		return ret;
	}
	
	// prepare exec params
	char tmp[256];
	std::vector<std::string> params;
	
	// argv[0], set to ffmpeg bin.
	// The  execv()  and  execvp() functions ....
	// The first argument, by convention, should point to 
	// the filename associated  with  the file being executed.
	params.push_back(ffmpeg);
	
	// input.
	params.push_back("-f");
	params.push_back("flv");
	
	params.push_back("-i");
	params.push_back(input);
	
	// build the filter
	if (!vfilter.empty()) {
		std::vector<std::string>::iterator it;
		for (it = vfilter.begin(); it != vfilter.end(); ++it) {
			std::string p = *it;
			if (!p.empty()) {
				params.push_back(p);
			}
		}
	}
	
	// video specified.
	params.push_back("-vcodec");
	params.push_back(vcodec);
	
	params.push_back("-b:v");
	snprintf(tmp, sizeof(tmp), "%d", vbitrate * 1000);
	params.push_back(tmp);
	
	params.push_back("-r");
	snprintf(tmp, sizeof(tmp), "%.2f", vfps);
	params.push_back(tmp);
	
	params.push_back("-s");
	snprintf(tmp, sizeof(tmp), "%dx%d", vwidth, vheight);
	params.push_back(tmp);
	
	// TODO: add aspect if needed.
	params.push_back("-aspect");
	snprintf(tmp, sizeof(tmp), "%d:%d", vwidth, vheight);
	params.push_back(tmp);
	
	params.push_back("-threads");
	snprintf(tmp, sizeof(tmp), "%d", vthreads);
	params.push_back(tmp);
	
	params.push_back("-profile:v");
	params.push_back(vprofile);
	
	params.push_back("-preset");
	params.push_back(vpreset);
	
	// vparams
	if (!vparams.empty()) {
		std::vector<std::string>::iterator it;
		for (it = vparams.begin(); it != vparams.end(); ++it) {
			std::string p = *it;
			if (!p.empty()) {
				params.push_back(p);
			}
		}
	}
	
	// audio specified.
	params.push_back("-acodec");
	params.push_back(acodec);
	
	params.push_back("-b:a");
	snprintf(tmp, sizeof(tmp), "%d", abitrate * 1000);
	params.push_back(tmp);
	
	params.push_back("-ar");
	snprintf(tmp, sizeof(tmp), "%d", asample_rate);
	params.push_back(tmp);
	
	params.push_back("-ac");
	snprintf(tmp, sizeof(tmp), "%d", achannels);
	params.push_back(tmp);
	
	// aparams
	if (!aparams.empty()) {
		std::vector<std::string>::iterator it;
		for (it = aparams.begin(); it != aparams.end(); ++it) {
			std::string p = *it;
			if (!p.empty()) {
				params.push_back(p);
			}
		}
	}

	// output
	params.push_back("-f");
	params.push_back("flv");
	
	params.push_back("-y");
	params.push_back(output);
	
	// TODO: fork or vfork?
	if ((pid = fork()) < 0) {
		ret = ERROR_ENCODER_FORK;
		srs_error("vfork process failed. ret=%d", ret);
		return ret;
	}
	
	// child process: ffmpeg encoder engine.
	if (pid == 0) {
		// memory leak in child process, it's ok.
		char** charpv_params = new char*[params.size() + 1];
		for (int i = 0; i < (int)params.size(); i++) {
			std::string p = params[i];
			charpv_params[i] = (char*)p.c_str();
		}
		// EOF: NULL
		charpv_params[params.size()] = NULL;
		
		// TODO: execv or execvp
		ret = execv(ffmpeg.c_str(), charpv_params);
		if (ret < 0) {
			fprintf(stderr, "fork ffmpeg failed, errno=%d(%s)", 
				errno, strerror(errno));
		}
		exit(ret);
	}

	// parent.
	if (pid > 0) {
		started = true;
		srs_trace("vfored ffmpeg encoder engine, pid=%d", pid);
		return ret;
	}
	
	return ret;
}

void SrsFFMPEG::stop()
{
	if (!started) {
		return;
	}
}

SrsEncoder::SrsEncoder()
{
	tid = NULL;
	loop = false;
}

SrsEncoder::~SrsEncoder()
{
	on_unpublish();
}

int SrsEncoder::parse_scope_engines()
{
	int ret = ERROR_SUCCESS;
	
	// parse all transcode engines.
	SrsConfDirective* conf = NULL;
	
	// parse vhost scope engines
	std::string scope = "";
	if ((conf = config->get_transcode(vhost, "")) != NULL) {
		if ((ret = parse_transcode(conf)) != ERROR_SUCCESS) {
			srs_error("parse vhost scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	// parse app scope engines
	scope = app;
	if ((conf = config->get_transcode(vhost, app)) != NULL) {
		if ((ret = parse_transcode(conf)) != ERROR_SUCCESS) {
			srs_error("parse app scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	// parse stream scope engines
	scope += "/";
	scope += stream;
	if ((conf = config->get_transcode(vhost, app + "/" + stream)) != NULL) {
		if ((ret = parse_transcode(conf)) != ERROR_SUCCESS) {
			srs_error("parse stream scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsEncoder::on_publish(std::string _vhost, std::string _port, std::string _app, std::string _stream)
{
	int ret = ERROR_SUCCESS;

	vhost = _vhost;
	port = _port;
	app = _app;
	stream = _stream;

	ret = parse_scope_engines();
	
	// ignore the loop encoder
	if (ret == ERROR_ENCODER_LOOP) {
		ret = ERROR_SUCCESS;
	}
	
	// return for error or no engine.
	if (ret != ERROR_SUCCESS || ffmpegs.empty()) {
		return ret;
	}
    
    // start thread to run all encoding engines.
    srs_assert(!tid);
    if((tid = st_thread_create(encoder_thread, this, 1, 0)) == NULL) {
		ret = ERROR_ST_CREATE_FORWARD_THREAD;
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    
	return ret;
}

void SrsEncoder::on_unpublish()
{
	if (tid) {
		loop = false;
		st_thread_interrupt(tid);
		st_thread_join(tid, NULL);
		tid = NULL;
	}

	clear_engines();
}

void SrsEncoder::clear_engines()
{
	std::vector<SrsFFMPEG*>::iterator it;
	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		srs_freep(ffmpeg);
	}
	ffmpegs.clear();
}

SrsFFMPEG* SrsEncoder::at(int index)
{
	return ffmpegs[index];
}

int SrsEncoder::parse_transcode(SrsConfDirective* conf)
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(conf);
	
	// enabled
	if (!config->get_transcode_enabled(conf)) {
		srs_trace("ignore the disabled transcode: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// ffmpeg
	std::string ffmpeg_bin = config->get_transcode_ffmpeg(conf);
	if (ffmpeg_bin.empty()) {
		srs_trace("ignore the empty ffmpeg transcode: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// get all engines.
	std::vector<SrsConfDirective*> engines;
	config->get_transcode_engines(conf, engines);
	if (engines.empty()) {
		srs_trace("ignore the empty transcode engine: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// create engine
	for (int i = 0; i < (int)engines.size(); i++) {
		SrsConfDirective* engine = engines[i];
		if (!config->get_engine_enabled(engine)) {
			srs_trace("ignore the diabled transcode engine: %s %s", 
				conf->arg0().c_str(), engine->arg0().c_str());
			continue;
		}
		
		SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
		
		if ((ret = ffmpeg->initialize(vhost, port, app, stream, engine)) != ERROR_SUCCESS) {
			srs_freep(ffmpeg);
			
			// if got a loop, donot transcode the whole stream.
			if (ret == ERROR_ENCODER_LOOP) {
				clear_engines();
				break;
			}
			
			srs_error("invalid transcode engine: %s %s", 
				conf->arg0().c_str(), engine->arg0().c_str());
			return ret;
		}

		ffmpegs.push_back(ffmpeg);
	}
	
	return ret;
}

int SrsEncoder::cycle()
{
	int ret = ERROR_SUCCESS;
	
	// start all ffmpegs.
	std::vector<SrsFFMPEG*>::iterator it;
	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		if ((ret = ffmpeg->start()) != ERROR_SUCCESS) {
			srs_error("ffmpeg start failed. ret=%d", ret);
			return ret;
		}
	}
	
	return ret;
}

void SrsEncoder::encoder_cycle()
{
	int ret = ERROR_SUCCESS;
	
	log_context->generate_id();
	srs_trace("encoder cycle start");
	
	while (loop) {
		if ((ret = cycle()) != ERROR_SUCCESS) {
			srs_warn("encoder cycle failed, ignored and retry, ret=%d", ret);
		} else {
			srs_info("encoder cycle success, retry");
		}
		
		if (!loop) {
			break;
		}
		
		st_usleep(SRS_ENCODER_SLEEP_MS * 1000);
	}
	
	// kill ffmpeg when finished and it alive
	std::vector<SrsFFMPEG*>::iterator it;
	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		ffmpeg->stop();
	}
	
	srs_trace("encoder cycle finished");
}

void* SrsEncoder::encoder_thread(void* arg)
{
	SrsEncoder* obj = (SrsEncoder*)arg;
	srs_assert(obj != NULL);
	
	obj->loop = true;
	obj->encoder_cycle();
	
	return NULL;
}

