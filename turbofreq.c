#define _XOPEN_SOURCE 500	/* pread(2), pwrite(2) */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>

#include "bitops.h"

#define	IA32_PERF_GLOBAL_CTRL	0x38F
#define 	IA32_FIXED_CTR1		0x30A	/* CPU_Unhalted.Core */
#define 	IA32_FIXED_CTR2		0x30B	/* CPU_Unhalted.Ref */
#define 	IA32_FIXED_CNTR_CTRL		0x38D	/* P.1138 */

#define 	MSR_TSC	0x10
#define 	MSR_APERF	0xE8
#define 	MSR_MPERF	0xE7
#define	MSR_PLATFORM_INFO	0xCE

#define	TSC_ORDER	1000000000

/* CPUID.EAX[15:8] */
#define get_nr_pmc(eax)	(eax >> 8) & 0x0F
/* CPUID.EAX[0:7] */
#define get_fixed_cntr_ver(eax)	(eax & 0x0F)

/*
* root権限, msr.koが必要
* modprobe msr
*/

double bus_clock;
int nr_cpus;
char *progname;

void usage(void)
{
	fprintf(stderr, "%s: [--csv] [-inteldoc|-turbostat] [-nehalem|-sandybridge]\n",
		progname);
}

void put_msr(int cpu, off_t offset, unsigned long long val)
{
	ssize_t retval;
	int fd;
	char pathname[32];

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_WRONLY);	/* 書き込みモードでオープン */
	if (fd < 0) {
		perror(pathname);
		//need_reinitialize = 1;
	}

	retval = pwrite(fd, &val, sizeof(unsigned long long), offset);
	printf("retval = %d\n", (int)retval);

	if (retval != sizeof(unsigned long long)) {
		fprintf(stderr, "cpu%d pread(..., 0x%zx) = %jd\n",
			cpu, offset, retval);
		exit(-2);
	}

	close(fd);
}

unsigned long long get_msr(int cpu, off_t offset)
{
	ssize_t retval;
	int fd;
	unsigned long long msr;
	char pathname[32];

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);	/* 読み込みモードでオープン */
	if (fd < 0) {
		perror(pathname);
		//need_reinitialize = 1;
		return 0;
	}

	retval = pread(fd, &msr, sizeof msr, offset);
	if (retval != sizeof msr) {
		fprintf(stderr, "cpu%d pread(..., 0x%zx) = %jd\n",
			cpu, offset, retval);
		exit(-2);
	}

	close(fd);
	return msr;
}

/*
* Description:CPUID命令を実行して結果を引数のアドレスに格納する関数
* @eax:CPUID命令を実行した後のEAXレジスタの値を格納する変数のアドレス
* @ebx:CPUID命令を実行した後のEBXレジスタの値を格納する変数のアドレス
* @ecx:CPUID命令を実行した後のECXレジスタの値を格納する変数のアドレス
* @edx:CPUID命令を実行した後のEDXレジスタの値を格納する変数のアドレス
*/
void exec_cpuid(unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx)
{
	unsigned int init = *eax;

	__asm__ ("cpuid\n\t"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx): "0"(init));

}

void setup_fixed_cntr_ctrl(int ver)
{
	u64 reg = 0;
	int i;

	if(ver == 2){
		/* CNTR0 - CNTR2を有効にする(All ring) */
		set_nbit64(&reg, 0);
		set_nbit64(&reg, 1);
		set_nbit64(&reg, 4);
		set_nbit64(&reg, 5);
		set_nbit64(&reg, 8);
		set_nbit64(&reg, 9);

		for(i = 0; i < nr_cpus; i++){
			put_msr(i, IA32_FIXED_CNTR_CTRL, reg);
		}
	}
	else if(ver == 3){
		set_nbit64(&reg, 0);
		set_nbit64(&reg, 1);
		//set_nbit64(&reg, 2);	/* Any Thread */
		set_nbit64(&reg, 4);
		set_nbit64(&reg, 5);
		//set_nbit64(&reg, 6);	/* Any Thread */
		set_nbit64(&reg, 8);
		set_nbit64(&reg, 9);
		//set_nbit64(&reg, 10);	/* Any Thread */

		for(i = 0; i < nr_cpus; i++){
			put_msr(i, IA32_FIXED_CNTR_CTRL, reg);
		}

	}
	else{
		printf("err fixed_cntr_ver = %d\n", ver);
		exit(EXIT_FAILURE);
	}
}


void setup_perf_global_ctrl(int nr_pmc)
{
	int i;
	u64 reg = 0;

	/* Fixed CounterのEnable bitを立てる */
	set_nbit64(&reg, 32);
	set_nbit64(&reg, 33);
	set_nbit64(&reg, 34);

	//print_binary64(reg);

	/* PMC0 - PMC($nr_cpus)のenable bitを立てる */
	for(i = 0; i < nr_cpus; i++){
		put_msr(i, IA32_PERF_GLOBAL_CTRL, reg);
		printf("%llu, ", get_msr(i, IA32_PERF_GLOBAL_CTRL));
	}
}

#define PERIOD	45

enum sample_mode{
	TURBOSTAT,	/* turbostatのアルゴリズム */
	INTELDOC	/* Intel推奨のアルゴリズム */
};

void cmdline(int argc, char *argv[], enum sample_mode *mode, int *csv_flag)
{
	int i;

	progname = argv[0];

	if(argc > 4){
		usage();
		exit(EXIT_FAILURE);
	}
	else{
		for(i = 1; i < argc; i++){
			if(strcmp("--csv", argv[i]) == 0){
				*csv_flag = 1;
			}
			else if(strcmp("-turbostat", argv[i]) == 0){
				*mode = TURBOSTAT;
			}
			else if(strcmp("-inteldoc", argv[i]) == 0){
				*mode = INTELDOC;
			}
			else if(strcmp("-nehalem", argv[i]) == 0){
				bus_clock = (double)0.13333;
			}
			else if(strcmp("-sandybridge", argv[i]) == 0){
				bus_clock = (double)0.10000;
			}
			else{
				usage();
				exit(EXIT_FAILURE);
			}
		}
	}
}

FILE *f;

void alloc_csv(enum sample_mode mode)
{
	int i;

	if(f == NULL){
		perror("fopen");
		exit(EXIT_FAILURE);
	}
	else{

		if(mode == TURBOSTAT){
			fprintf(f, "SAMPLE MODE:TURBOSTAT ALGORITHM\n,");
		}
		else if(mode == INTELDOC){
			fprintf(f, "SAMPLE MODE:INTEL DOCUMENT ALGORITHM\n,");
		}

		for(i = 0; i < nr_cpus; i++){
			fprintf(f, "CPU#%d,", i);
		}

		fprintf(f, "\n");
	}
}

/* turbostatsのアルゴリズム */
void *thread_turbostat(void *p)
{
	u64 pmc1, pmc2, pmc3;
	u64 pmc1_last[nr_cpus], pmc2_last[nr_cpus], pmc3_last[nr_cpus];
	long long sub_pmc1, sub_pmc2, sub_pmc3;
	double current;
	int i, cnt = 0;

	/* pmc_lastを用意する */
	for(i = 0; i < nr_cpus; i++){
		pmc1_last[i] = get_msr(i, MSR_APERF);
		pmc2_last[i] = get_msr(i, MSR_MPERF);
		pmc3_last[i] = get_msr(i, MSR_TSC);
	}

	while(cnt < PERIOD){
		sleep(1);

		if(f == NULL){
			puts("----------------");
		}

		for(i = 0; i < nr_cpus; i++){
			if(i == 0 && f){
				fprintf(f, "%d,", cnt);
			}

			pmc1 = get_msr(i, MSR_APERF);
			pmc2 = get_msr(i, MSR_MPERF);
			pmc3 = get_msr(i, MSR_TSC);


			sub_pmc1 = pmc1 - pmc1_last[i];
			sub_pmc2 = pmc2 - pmc2_last[i];
			sub_pmc3 = pmc3 - pmc3_last[i];

			current = (double)sub_pmc3 / TSC_ORDER * (double)sub_pmc1 / (double)sub_pmc2;

			if(f == NULL){
				printf("CPU#%d freq = %1.2f GHz\n", i, current);
			}
			else{
				fprintf(f, "%1.2f,", current);
			}

			//printf("CPU#%d freq = %1.2f GHz\n", i, current);

			if(i == nr_cpus - 1 && f){
				fprintf(f, "\n");
			}

			pmc1_last[i] = pmc1;
			pmc2_last[i] = pmc2;
			pmc3_last[i] = pmc3;
		}

		cnt++;

	}

	return NULL;
}

/* Intel推奨のアルゴリズム */
void *thread_inteldoc(void *p)
{
	u64 pmc1, pmc2;
	u64 pmc1_last[nr_cpus], pmc2_last[nr_cpus];
	long long sub_pmc1, sub_pmc2;
	double current;
	int i, cnt = 0;

	u64 base_op_ratio = (get_msr(0, MSR_PLATFORM_INFO) & 0xFF00) >> 8;

	/* pmc_lastを用意する */
	for(i = 0; i < nr_cpus; i++){
		pmc1_last[i] = get_msr(i, IA32_FIXED_CTR1);
		pmc2_last[i] = get_msr(i, IA32_FIXED_CTR2);
	}

	while(cnt < PERIOD){
		sleep(1);

		if(f == NULL){
			puts("----------------");
		}

		for(i = 0; i < nr_cpus; i++){
			if(i == 0 && f){
				fprintf(f, "%d,", cnt);
			}

			pmc1 = get_msr(i, IA32_FIXED_CTR1);
			pmc2 = get_msr(i, IA32_FIXED_CTR2);

			sub_pmc1 = pmc1 - pmc1_last[i];
			sub_pmc2 = pmc2 - pmc2_last[i];

			current = base_op_ratio * bus_clock * ((double)sub_pmc1 / (double)sub_pmc2);
			//current = base_op_ratio * (double)0.13333 * ((double)sub_pmc1 / (double)sub_pmc2);

			if(f == NULL){
				printf("CPU#%d freq = %1.2f GHz\n", i, current);
			}
			else{
				fprintf(f, "%1.2f,", current);
			}


			if(i == nr_cpus - 1 && f){
				fprintf(f, "\n");
			}

			pmc1_last[i] = pmc1;
			pmc2_last[i] = pmc2;
		}

		cnt++;

	}

	return NULL;
}

int main(int argc, char *argv[])
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int nr_pmc, fixed_cntr_ver;
	int csv_flag = 0;

	pthread_t thread;
	enum sample_mode mode;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	cmdline(argc, argv, &mode, &csv_flag);

	if(csv_flag){
		f = fopen("turbofreq.csv", "w");
		alloc_csv(mode);
	}

	/* CPUID.EAXから必要なレポートを取得 */
	eax = 0x0A;
	exec_cpuid(&eax, &ebx, &ecx, &edx);

	fixed_cntr_ver = get_fixed_cntr_ver(eax);
	nr_pmc = get_nr_pmc(eax);

	printf("nr_pmc = %d\n", nr_pmc);

	/* FIXED_CNTR_CTLを設定 */
	setup_fixed_cntr_ctrl(fixed_cntr_ver);

	/* IA32_PERF_GLOBAL_CTRLの設定 */
	setup_perf_global_ctrl(nr_pmc);

	if(mode == TURBOSTAT){
		if((pthread_create(&thread, NULL, thread_turbostat, NULL)) == -1){
			puts("pthread_create(3) error");
			exit(EXIT_FAILURE);
		}
	}
	else if(mode == INTELDOC){
		if((pthread_create(&thread, NULL, thread_inteldoc, NULL)) == -1){
			puts("pthread_create(3) error");
			exit(EXIT_FAILURE);
		}
	}

	pthread_join(thread, NULL);	/* mainスレッドはここでブロックされる */

	if(f == NULL){
		;
	}
	else{
		fclose(f);
	}

	return 0;
}

