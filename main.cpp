#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

class RecFile
{
	FILE *                                   fp       = 0;
	const char *                             rec_file = "kbdrec.tsv";
	std::map<std::string, int>               rec;
	std::vector<std::pair<std::string, int>> sorted()
	{
		std::vector<std::pair<std::string, int>> out;
		std::copy(rec.begin(), rec.end(), std::back_inserter(out));
		std::sort(out.begin(), out.end(),
		          [](const std::pair<std::string, int> &l, const std::pair<std::string, int> &r) {
			          return l.second > r.second;
		          });
		return out;
	}

public:
	void load()
	{
		fp = fopen(rec_file, "rb");
		if (fp == 0) {
			printf("[!] no old record (%s), will create later.\n", rec_file);
			return;
		}
		char key_name[30];
		int  count = 0;
		while (1) {
			if (fscanf(fp, "%s%d", key_name, &count) == EOF) {
				break;
			}
			rec[key_name] = count;
		}
		fclose(fp);
	}
	void p_file()
	{
		FILE *fp = fopen(rec_file, "wb+");
		if (fp == 0) {
			printf("[!] cannot open and write (%s), will not create.\n", rec_file);
			return;
		}

		auto out = sorted();

		for (auto &p : out) {
			fprintf(fp, "%s\t%d\n", p.first.c_str(), p.second);
		}
		fclose(fp);
	}
	void p_scr()
	{
		auto out   = sorted();
		int  total = 0;

		for (auto &p : out) {
			printf("%15s %6d\n", p.first.c_str(), p.second);
			total += p.second;
		}
		printf("[+] Total: %d\n", total);
	}
	void save()
	{
		p_file();
		p_scr();
	}
	void push(const std::string &key)
	{
		++rec[key];
	}
};

class Speed
{
	static const int his_sz = 100;
	static const int str_sz = 100;

	uint64_t history[his_sz];
	uint64_t count;
	uint64_t last;
	char     curstr[str_sz];

	inline uint64_t cur_time()
	{
		struct timeval tv;
		gettimeofday(&tv, 0);
		return (uint64_t)(tv.tv_sec) * 1000000 + tv.tv_usec;
	}

public:
	void init()
	{
		count = 0;
		last  = cur_time();
	}
	void push()
	{
		uint64_t dur = cur_time() - last;
		if (dur < 3 * 1000000) { // ignore types which is too slow
			history[count++ % his_sz] = dur;
		}
		last = cur_time();
	}
	double get()
	{
		uint64_t div = count > his_sz ? his_sz : count;
		uint64_t sum = 0;
		for (uint64_t i = 0; i < div; ++i) {
			sum += history[i];
		}
		return (double)div / ((double)sum / 1000000) * 60;
	}
	char *getcur()
	{
		struct timeval tv;
		gettimeofday(&tv, 0);
		struct tm *tm_info;
		tm_info = localtime(&(tv.tv_sec));
		strftime(curstr, str_sz, "%m-%d %T", tm_info);
		return curstr;
	}
};

volatile int alive = 1;

void sig(int _)
{
	printf("\n");
	alive = 0;
}

RecFile rf;

void dump(int _)
{
	printf("\n");
	rf.p_scr();
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("%s <ev-id>\n", argv[0]);
		printf("To get event ID with name, run `evtest'\n");
		printf("The ID may be different after reboot\n");
		return -1;
	}

	printf("[+] PID: %d\n", getpid());

	std::string pn = "sudo evtest /dev/input/event" + std::string(argv[1]) +
	                 " | stdbuf -o0 grep '(KEY.*1$' | stdbuf -o0 sed -E 's/.*KEY_(.*)\\).*/\\1/g'";
	FILE *p_ev = popen(pn.c_str(), "r");
	if (p_ev == 0) {
		return 1;
	}

	signal(SIGINT, sig);   // ctrl+c
	signal(SIGPIPE, sig);  // upstream gone
	signal(SIGTERM, sig);  // kill
	signal(SIGUSR1, dump); // print while running

	rf.load();

	char  key_name[30];
	int   typed = 0;
	Speed ss;
	ss.init();
	winsize win;
	while (alive) {
		if (fgets(key_name, 30, p_ev) == 0) {
			break;
		}
		for (int i = 0; i < 30; ++i) {
			if (key_name[i] == 0x0A) {
				key_name[i] = 0;
				break;
			}
		}
		rf.push(key_name);
		++typed;
		ss.push();
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &win);
		if (typed % (win.ws_col - 34) == 0) {
			printf("%6d (%3.2lf/min) %s\n", typed, ss.get(), ss.getcur());
		} else {
			printf(".");
		}
		fflush(stdout);

		if (typed % 2000 == 0) {
			rf.p_file();
		}
	}

	pclose(p_ev);

	rf.save();

	return 0;
}
