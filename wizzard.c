/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** installation wizzard                                                      **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *check_mISDN(void)
{
}

char *install_mISDN(void)
{
}

char *check_kernel(void)
{
}

char *install_kernel(void)
{
}

char *check_includes(void)
{
}

char *install_includes(void)
{
}

char *check_device(void)
{
}

char *install_device(void)
{
}

char *check_lib(void)
{
}

char *install_lib(void)
{
}

char *check_isdnnet(void)
{
}

char *install_isdnnet(void)
{
}

char *check_pbx(void)
{
}

char *install_pbx(void)
{
}

char *check_mISDNrc(void)
{
}

char *install_mISDNrc(void)
{
}


struct jobs {
	char *name;
	(char *(check))(void);
	(char *(install))(void);
} jobs[] = {
	{ "Install mISDN to kernel Source.", check_mISDN, install_mISDN },
	{ "Compile and install Kernel.", check_kernel, install_kernel },
	{ "Copy user space includes.", check_includes, install_includes },
	{ "Create \"/dev/mISDN\" device", check_device, install_device },
	{ "Compile mISDN device library.", check_lib, install_lib },
	{ "Compile mISDN NT-mode library.", check_isdnnet, install_isdnnet },
	{ "Compile and install PBX4Linux.", check_pbx, install_pbx },
	{ "Create mISDNrc to load mISDN.", check_mISDNrc, install_mISDNrc },
	{ NULL, NULL, NULL},
};


int main(int argc, char *argv[])
{
	int allok = 1;
	int i;
	char *ret;
	char input[256];

	printf("\nWelcome to PBX4Linux installation wizzard.\n\n");

	again:

	/* check what to do */
	i = 0;
	while(jobs[i].name)
	{
		printf("Checking: %s - ", jobs[i].name);
		fflush(stdout);
		ret = jobs[i].check();
		if (ret == NULL)
			printf("OK\n");
		else {
			printf("%s\n", ret);
			allok = 0;
		}
		i++;
	}

	/* if all ok */
	if (allok)
	{
		printf("\nEverything seems to be correctly installed. Do you like to continue? (y/n)");
		fflush(stdout);
		do {
			scanf("%s", input);
		} while(input[0] != 'y' && input[0] != 'n');
		if (input[0] == 'n')
			return(0);
		
	}

	/* select installation step(s) */
	printf("\nPlease select one of the following install options:\n");
	printf("a - Complete installation with all of the following steps\n");
	i = 0;
	while(jobs[i].name)
	{
		printf("%d - Step %d: %s\n", i+1, i+1, jobs[i].name);
		i++;
	}
	printf("x - Exit wizzard.\n");
	printf("\n(a/1-%d/x)", i);
	fflush(stdout);
	do {
		scanf("%s", input);
	} while(input[0]!='a' && (input[0]<'1' || input[0]>('0'+i)) && input[0]!='x');
	if (input[0] == 'x')
		return(0);
	i = 0;
	while(jobs[i].name)
	{
		if (input[0]=='a' || (input[0]-'1')==i)
		{
			printf("\nDoing Step %d: %s\n", i+1, jobs[i].name);
			ret = jobs[i].check();
			if (ret)
				printf("It is required to continue with this step. Dou you want to continue? (y/n)");
			else
				printf("It is not required to continue with this step. Still want to continue? (y/n)");
			fflush(stdout);
			do {
				scanf("%s", input);
			} while(input[0] != 'y' && input[0] != 'n');
			if (input[0] == 'n')
				i++;
				continue;
			}
			ret = jobs[i].install();
			if (ret)
			{
				printf("Failed to install step: %s\n", jobs[i].name);
				printf("%s\n", ret);
				printf("Do you like to retry? (y/n)");
				fflush(stdout);
				do {
					scanf("%s", input);
				} while(input[0] != 'y' && input[0] != 'n');
				if (input[0] == 'y')
					continue;
				}
				break;
			}
		i++;
	}
	goto again;
}

 
