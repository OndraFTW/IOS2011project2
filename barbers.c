/*
 * Soubor:  barbers.c
 * Datum:   2011/18/04
 * Autor:   Ondrej Slampa, xslamp01@stud.fit.vutbr.cz
 * Projekt: Synchronizace procesu
 * Popis:   Programje implementaci upravene varianty synchronizacniho problemu
 *          spiciho holice
 */

#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<string.h>
#include<time.h>
#include<limits.h>
#include<errno.h>

#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<fcntl.h>
#include<semaphore.h>
#include<signal.h>
#include<sched.h>

/*
* Definice maker
*/

//pocet sdilenych promennych a jednotlive promenne
#define NSHRVAR 3
#define NUMBER shrMem[0]
#define FREECHAIRS shrMem[1]
#define NPROC shrMem[2]

//pocet semaforu a jednotlive semafory
#define NSEM 6
#define SEMNUMBER semArray[0]
#define SEMFREECHAIRS semArray[1]
#define SEMCHAIR semArray[2]
#define SEMCUSTOMER semArray[3]
#define SEMBARBER semArray[4]
#define SEMNPROC semArray[5]

/** Chybove kody programu. */
enum tecodes
{
	EOK=0,       /**< Bez chyby. */
	EHELP,       /**< Vypsat napovedu */
	ECLWRONG,    /**< Chybny prikazovy radek. */
	EOFILE,      /**< Nastala chyba pri otevirani souboru. */
	ECFILE,      /**< Nastala chyba pri zavirani souboru. */
	EPROC,       /**< Nastala chyba pri praci s procesem. */
	ESHRMEM,     /**< Nastala chyba pri praci se sdilenou pameti. */
	ESEM,        /**< Nastala chyba pri praci se semaforem. */
	EMALLOC,     /**< Nastala chyba pri alokaci pameti. */
	EFILE,       /**< Nastala chyba pri praci se souborem. */
	ESIG,        /**< Nekteremu z procesu byl poslam signal na ukonceni. */
	EUNKNOWN,    /**< Neznama chyba. */
};

/** Hlaseni odpovidajici chybovym kodum. */
const char *ECODEMSG[] =
{
	/*EOK*/
	"Vse v poradku.\n",
	/*EHELP*/
	"",
	/*ECLWRONG*/
	"Chybne parametry prikazoveho radku.\n",
	/*EOFILE*/
	"Nastala chyba pri otevirani souboru.\n",
	/*ECFILE*/
	"Nastala chyba pri zavirani souboru.\n",
	/*EFORK*/
	"Nastala chyba pri praci s procesem.\n",
	/*ESHRMEM*/
	"Nastala chyba pri praci se sdilenou pameti.\n",
	/*ESEM*/
	"Nastala chyba pri praci se semaforem.\n",
	/*EMALLOC*/
	"Nastala chyba pri alokaci pameti.\n",
	/*EFILE*/
	"Nastala chyba pri praci se souborem.\n",
	/*ESIG*/
	"Nekteremu z procesu byl poslam signal na ukonceni.\n",
	/*EUNKNOWN*/
	"Nastala nepredvidana chyba.\n",
};

/** Napoveda */
const char *HELPMSG =
	"Program: Synchronizace procesu.\n"
	"Autor: Ondrej Slampa (c) 2011\n"
	"Program je implementaci upravene varianty synchronizacniho problemu "
	"spiciho holice.\n"
	"Pouziti: barbers -h\n"
	"         barbers Q GenC GenB N F\n"
	"Popis parametru:\n"
	"-h vypise tuto napovedu k programu\n"
	"Q pocet zidli\n"
	"GenC rozsah pro generovani zakazniku [ms]\n"
	"GenB rozsah pro generovani doby obsluhy [ms]\n"
	"N pocet zakazniku\n"
	"F nazev vystupniho souboru, hodnota \"-\" znamena stdout\n";

/*
* Struktura na ulozeni informaci z prikazove radky.
*/

typedef struct params
{
	int ecode;             /**< Chybovy kod programu, odpovida vyctu tecodes. */
	unsigned int chairs;   /**< Pocet zidli. */
	unsigned int GenC;     /**< Rozsah pro generovani zakazniku [ms]. */
	unsigned int GenB;     /**< Rozsah pro generovani doby obsluhy [ms]. */
	unsigned int customers;/**< Pocet zakazniku. */
	char *output;          /**< Vystupni soubor. */
} TParams;

//PID rodice
volatile pid_t parentPID;
//cislo chyby, ktera nastala
volatile int err;

/*
* Funkcni prototypy
*/

TParams getParams(int argc, char *argv[]);
void terminate(unsigned int *shrMem, sem_t *semArray[NSEM], pid_t *childrenPID,\
	unsigned int N, int shareID);
void signalHandler(int sigNum);
void customer(unsigned int *shrMem, unsigned int i, sem_t *semArray[NSEM]);
void barber(unsigned int *shrMem, unsigned int chairs, sem_t *semArray[NSEM], \
	unsigned int GenB);

////////////////////////////////////////////////////////////////////////////////
/*
* Jednotlive funkce.
*/


/**
* Zpracuje argumenty prikazoveho radku a vrati je ve strukture Tparams.
* Pokud je format argumentu chybny, vrati chybu ve strukture Tparams.
* @param argc Pocet argumentu.
* @param argv Pole textovych retezcu s argumenty.
*/
 
TParams getParams(int argc, char *argv[])
{
	//inicializace struktury TParams
	TParams result=
	{
		.ecode=EOK,
		.chairs=0,
		.GenC=0,
		.GenB=0,
		.customers=0,
		.output=NULL
	};

	//pokud na prikazovem radku byly dva parametry a druhy byl -h
	if (argc == 2 && strcmp("-h", argv[1]) == 0)
	{	
		result.ecode=EHELP;
	}
	//pokud na prikazovem radku je spravny pocet parametru
	else if(argc==6)
	{
		errno=0;
		char *ep;
		unsigned long conv;
				
		//prevod cisel na prikazove radce
		//pocet zidli
		conv=strtoul(argv[1], &ep, 10);
		if(errno!=0 || *ep!='\0' || conv>UINT_MAX)
		{
			result.ecode=ECLWRONG;
			return result;
		}
		
		result.chairs=(unsigned int)conv;
		
		//doba generace zakaznika
		conv=strtoul(argv[2], &ep, 10);
		if(errno!=0 || *ep!='\0' || conv>UINT_MAX)
		{
			result.ecode=ECLWRONG;
			return result;
		}
		
		result.GenC=(unsigned int)conv;
		
		//doba obsluhy
		conv=strtoul(argv[3], &ep, 10);
		if(errno!=0 || *ep!='\0' || conv>UINT_MAX)
		{
			result.ecode=ECLWRONG;
			return result;
		}
		
		result.GenB=(unsigned int)conv;
		
		//pocet zakazniku
		conv=strtoul(argv[4], &ep, 10);
		if(errno!=0 || *ep!='\0' || conv>(UINT_MAX-1))
		{
			result.ecode=ECLWRONG;
			return result;
		}
		
		result.customers=(unsigned int)conv;
		result.output=argv[5];	
	}
	//spatny puocet argumentu
	else
	{ 
		result.ecode=ECLWRONG;
	}

	return result;
}

/*
* Ostranni alokovane zdroje a vytvorene potomky.
* @param shrMem ukazatel na sdilenou pamet
* @param semArray pole ukazatelu na semafory
* @param childrenPID ukazatel na pole PID potomku
* @param N pocet potomku, kterri se musi odstranit
* @param shareID identifikator segmentu sdilene pameti
*/

void terminate(unsigned int *shrMem, sem_t *semArray[NSEM], pid_t *childrenPID,\
 unsigned int N, int shareID)
{
	//odstraneni prvnich N potomku
	for(unsigned int i=0; i<N; i++)
	{
		kill(childrenPID[i], SIGKILL);
	}
	
	free(childrenPID);
	
	sem_close(SEMNUMBER);
	sem_unlink("/SEMNUMBER");
	sem_close(SEMFREECHAIRS);
	sem_unlink("/SEMFREECHAIRS");
	sem_close(SEMCHAIR);
	sem_unlink("/SEMCHAIR");
	sem_close(SEMCUSTOMER);
	sem_unlink("/SEMCUSTOMER");
	sem_close(SEMBARBER);
	sem_unlink("/SEMBARBER");
	sem_close(SEMNPROC);
	sem_unlink("/SEMNPROC");

	shmdt(shrMem);
	shmctl(shareID, IPC_RMID, NULL);
}

/*
* Zpracovani prichoziho signalu.
* @param sigNum cislo signalu
*/

void signalHandler(int sigNum)
{
	//pokud byl signal poslan rodicovskemu procesu, ulozi se prislusna chyba
	//do chybove promenne (SIGUSR1 je chyba semaforu ostatni chyba - dosel 
	//signal na ukoceni programu)
	if(getpid()==parentPID)
	{
		err=sigNum==SIGUSR1?ESEM:ESIG;
	}
	//pokud byl signal poslan potomkovy preposle ho rodici
	else
	{
		kill(parentPID, sigNum);				
	}
}

/*
* Funkce zakaznika
* @param shrMem ukazatel na sdilene promenne
* @param i cislo zakaznika
* @param semArray ukazatel na semafory
*/

void customer(unsigned int *shrMem, unsigned int i, sem_t *semArray[NSEM])
{
	//zakaznik byl vytvoren
	if(sem_wait(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	NUMBER++;
	printf("%d: customer %d: created.\n", NUMBER, i);
	fflush(stdout);
	if(sem_post(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//zakaznik vstupuje do cekarny
	if(sem_wait(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}	
	if(sem_wait(SEMFREECHAIRS)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	NUMBER++;
	printf("%d: customer %d: enters.\n", NUMBER, i);
	fflush(stdout);
	
	//v cekarne neni misto a zakaznik odchazi
	if(FREECHAIRS==0)
	{
		if(sem_post(SEMFREECHAIRS)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		
		NUMBER++;
		printf("%d: customer %d: refused.\n", NUMBER, i);
		fflush(stdout);
		if(sem_post(SEMNUMBER)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		
		if(sem_wait(SEMNPROC)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		NPROC--;
		if(sem_post(SEMNPROC)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		
		return;
	}
	//v cekarne je misto
	else
	{
		FREECHAIRS--;
		if(sem_post(SEMFREECHAIRS)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		if(sem_post(SEMNUMBER)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
	}
	
	//zakaznik ceka nez ho holic pozve do kresla
	if(sem_wait(SEMCHAIR)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//zakaznik si sedne do holicova kresla
	if(sem_wait(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	NUMBER++;
	printf("%d: customer %d: ready.\n", NUMBER, i);
	fflush(stdout);
	
	if(sem_wait(SEMFREECHAIRS)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	FREECHAIRS++;
	if(sem_post(SEMFREECHAIRS)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	if(sem_post(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//zakaznik rekne holicovy, ze je pripraven se ostrihat
	if(sem_post(SEMCUSTOMER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//zakaznik ceka nez ho holic ostriha
	if(sem_wait(SEMBARBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//zakaznik odchazi obslouzen z holicstvi
	if(sem_wait(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	NUMBER++;
	printf("%d: customer %d: served.\n", NUMBER, i);
	fflush(stdout);
	
	//zakaznik rika holici, ze je odchazi
	if(sem_post(SEMCUSTOMER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	if(sem_post(SEMNUMBER)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	//snizeni poctu procesu o 1
	if(sem_wait(SEMNPROC)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	NPROC--;
	if(sem_post(SEMNPROC)==-1)
	{
		kill(parentPID, SIGUSR1);
		return;
	}
	
	return;
}

/*
* Funkce holice.
* @param shrMem ukazatel na sdilene promenne
* @param chairs pocet zidli v cekarne
* @param semArray ukazatel na semafory
* @param GenB maximalni doba obsluhy
*/

void barber(unsigned int *shrMem, unsigned int chairs, sem_t *semArray[NSEM], \
unsigned int GenB)
{
	//pocet zakazniku v cekarne
	unsigned int customers=0;
	//spi holic ?
	bool sleep=false;
	srandom(time(0));
	
	for(;;)
	{
		//pokud holic nespi zkontroluje cekarnu
		if(!sleep)
		{
			if(sem_wait(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			NUMBER++;
			printf("%d: barber: checks.\n", NUMBER);
			fflush(stdout);
			
			if(sem_wait(SEMFREECHAIRS)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			customers=chairs-FREECHAIRS;
			if(sem_post(SEMFREECHAIRS)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			if(sem_post(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
		}
		//jinak se je probuzen a zkonroluje pocet zakazniku v cekarne
		else
		{
			sleep=false;
			
			if(sem_wait(SEMFREECHAIRS)==-1/*true*/)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			customers=chairs-FREECHAIRS;
			if(sem_post(SEMFREECHAIRS)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
		}
		
		//pokud jsou v cekarns zakaznici obslouzi je
		if(customers>0)
		{
			//holic je pripraven ostrihat prvni zakaznika z cekarne
			if(sem_wait(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			NUMBER++;
			printf("%d: barber: ready.\n", NUMBER);
			fflush(stdout);
			if(sem_post(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			//holic si pozve zakaznika do kresla
			if(sem_post(SEMCHAIR)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			//pocka dokud si zakaznik nesedne
			if(sem_wait(SEMCUSTOMER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			//obslouzi ho
			usleep((random())%(GenB+1));
			
			//holic je hotov
			if(sem_wait(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			NUMBER++;
			printf("%d: barber: finished.\n", NUMBER);
			fflush(stdout);
			if(sem_post(SEMNUMBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			//propusti zakaznika z kresla
			if(sem_post(SEMBARBER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
			
			//pocka az zakaznik odejde z kresla
			if(sem_wait(SEMCUSTOMER)==-1)
			{
				kill(parentPID, SIGUSR1);
				return;
			}
		}
		
		//zkontroluje pocet lidi v cekarne
		if(sem_wait(SEMFREECHAIRS)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		customers=chairs-FREECHAIRS;
		if(sem_post(SEMFREECHAIRS)==-1)
		{
			kill(parentPID, SIGUSR1);
			return;
		}
		
		//pokud v cekarne nikdo neni usne
		if(customers==0)
		{
			sleep=true;
			
			//spanek
			for(;;)
			{
				//kontrola poctu potomku, pokud je jen jeden potomek znamena to,
				//ze je to holic samotny a tak se ukonci
				if(sem_wait(SEMNPROC)==-1)
				{
					kill(parentPID, SIGUSR1);
					return;
				}
				if(NPROC==1)
				{
					NPROC--;
					if(sem_post(SEMNPROC)==-1)
					{
						kill(parentPID, SIGUSR1);
						return;
					}
					return;
				}
				//pokud neni jediny potomek, tak ceka dokud ho naprobudi
				//zakaznik
				else 
				{
					if(sem_post(SEMNPROC)==-1)
					{
						kill(parentPID, SIGUSR1);
						return;
					}
					
					if(sem_wait(SEMFREECHAIRS)==-1)
					{
						kill(parentPID, SIGUSR1);
						return;
					}
					//pokud se pocet volnych zidli nerovna celkovemu poctu zidli
					//holic je vzbuzen
					if(FREECHAIRS!=chairs)
					{
						if(sem_post(SEMFREECHAIRS)==-1)
						{
							kill(parentPID, SIGUSR1);
							return;
						}
						break;
					}
					//jinak se vzda sveho procesoroveho casu,
					//aby zakaznici mohli prijit do cekarny
					else
					{
						if(sem_post(SEMFREECHAIRS)==-1)
						{
							kill(parentPID, SIGUSR1);
							return;
						}
						sched_yield();
					}
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
/**
* Hlavni program.
*/
 
int main(int argc, char *argv[])
{
	//nastaveni jmena rodice a chybove promenne
	parentPID=getpid();
	err=EOK;
	
	//struktury na zpracovani signalu a jejich inicializace
	struct sigaction signalAction;
	sigset_t signalSet;
	
	sigfillset(&signalSet);
	
	signalAction.sa_handler=signalHandler;
	signalAction.sa_mask=signalSet;
	signalAction.sa_flags=0;
	
	//zmena chovani programu pro urcite signaly
	sigaction(SIGUSR1, &signalAction, NULL);
	sigaction(SIGTERM, &signalAction, NULL);
	sigaction(SIGHUP, &signalAction, NULL);
	sigaction(SIGINT, &signalAction, NULL);
	sigaction(SIGQUIT, &signalAction, NULL);
	
	//printf("%ud\n", UINT_MAX);
	
	//zpracovani parametru prikazove radky
	TParams params=getParams(argc, argv);
	
	//vypis napovedy
	if(params.ecode==EHELP)
	{
		printf("%s", HELPMSG);
		return EXIT_SUCCESS;
	}
	//chyba pri zpracovani prikazove radky
	else if(params.ecode!=EOK)
	{
		fprintf(stderr, "%s", ECODEMSG[params.ecode]);
		return EXIT_FAILURE;
	}
	//jina chyba
	else if(err!=EOK)
	{
		fprintf(stderr, "%s", ECODEMSG[err]);
		return EXIT_FAILURE;
	}
	
	//alokace sdilenych promennych
    unsigned int *shrMem;
    int shareID;
    key_t IPCkey;
    
    if((IPCkey=ftok("./barbers", 'O'))==-1)
    {
		fprintf(stderr, "%s", ECODEMSG[ESHRMEM]);
		return EXIT_FAILURE;
    }
	
	if((shareID=shmget(IPCkey,NSHRVAR*sizeof(unsigned int),IPC_CREAT|0666))==-1)
	{
		fprintf(stderr, "%s", ECODEMSG[ESHRMEM]);
		return EXIT_FAILURE;
	}
	
	if((shrMem=(unsigned int*)shmat(shareID, NULL, 0))==(void *)-1)
	{
		fprintf(stderr, "%s", ECODEMSG[ESHRMEM]);
		return EXIT_FAILURE;
	}
	
	//inicializace sdilenych promennych
	NUMBER=0;
	FREECHAIRS=params.chairs;
	NPROC=params.customers+1;
	
	//pole semaforu
	sem_t *semArray[NSEM];
	
	//semafor pro cislo vypisu
	if((SEMNUMBER=sem_open("/SEMNUMBER", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 1))==SEM_FAILED)
	{
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}
	
	//semafor pro pocet volnych zidel v cekarne
	if((SEMFREECHAIRS=sem_open("/SEMFREECHAIRS", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 1))==SEM_FAILED)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}

	//semafor pro zidli holice
	if((SEMCHAIR=sem_open("/SEMCHAIR", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 0))==SEM_FAILED)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		sem_close(SEMFREECHAIRS);
		sem_unlink("/SEMFREECHAIRS");
		sem_close(SEMCHAIR);
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}
	
	//semafor pro akce zakaznika
	if((SEMCUSTOMER=sem_open("/SEMCUSTOMER", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 0))==SEM_FAILED)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		sem_close(SEMFREECHAIRS);
		sem_unlink("/SEMFREECHAIRS");
		sem_close(SEMCHAIR);
		sem_unlink("/SEMCHAIR");
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}
	
	//semafor pro akce holice
	if((SEMBARBER=sem_open("/SEMBARBER", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 0))==SEM_FAILED)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		sem_close(SEMFREECHAIRS);
		sem_unlink("/SEMFREECHAIRS");
		sem_close(SEMCHAIR);
		sem_unlink("/SEMCHAIR");
		sem_close(SEMCUSTOMER);
		sem_unlink("/SEMCUSTOMER");
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}
	
	//semafor pro pocet potomku
	if((SEMNPROC=sem_open("/SEMNPROC", O_CREAT, \
		S_IXOTH|S_IRUSR|S_IWUSR, 1))==SEM_FAILED)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		sem_close(SEMFREECHAIRS);
		sem_unlink("/SEMFREECHAIRS");
		sem_close(SEMCHAIR);
		sem_unlink("/SEMCHAIR");
		sem_close(SEMCUSTOMER);
		sem_unlink("/SEMCUSTOMER");
		sem_close(SEMBARBER);
		sem_unlink("/SEMBARBER");
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[ESEM]);
		return EXIT_FAILURE;
	}
	
	//pokud ma byt stdout presmerovam stane se tak
	if(strcmp(params.output, "-")!=0)
	{
		if((stdout=fopen(params.output, "w"))==NULL)
		{
			sem_close(SEMNUMBER);
			sem_unlink("/SEMNUMBER");
			sem_close(SEMFREECHAIRS);
			sem_unlink("/SEMFREECHAIRS");
			sem_close(SEMCHAIR);
			sem_unlink("/SEMCHAIR");
			sem_close(SEMCUSTOMER);
			sem_unlink("/SEMCUSTOMER");
			sem_close(SEMBARBER);
			sem_unlink("/SEMBARBER");
			sem_close(SEMNPROC);
			sem_unlink("/SEMNPROC");
			
			shmdt(shrMem);
			shmctl(shareID, IPC_RMID, NULL);
			
			fprintf(stderr, "%s", ECODEMSG[EFILE]);
		}
	}
	
	//pole s PID potomku
	pid_t *childrenPID;
	
	//alokace pole PID potomku
	if((childrenPID=malloc(sizeof(pid_t)*(params.customers+1)))==NULL)
	{
		sem_close(SEMNUMBER);
		sem_unlink("/SEMNUMBER");
		sem_close(SEMFREECHAIRS);
		sem_unlink("/SEMFREECHAIRS");
		sem_close(SEMCHAIR);
		sem_unlink("/SEMCHAIR");
		sem_close(SEMCUSTOMER);
		sem_unlink("/SEMCUSTOMER");
		sem_close(SEMBARBER);
		sem_unlink("/SEMBARBER");
		sem_close(SEMNPROC);
		sem_unlink("/SEMNPROC");
		
		if(strcmp(params.output, "-")!=0)
		{
			fclose(stdout);
		}
		
		shmdt(shrMem);
		shmctl(shareID, IPC_RMID, NULL);
		fprintf(stderr, "%s", ECODEMSG[EMALLOC]);
		return EXIT_FAILURE;
	}
	
	srandom(time(NULL));
	
	//pocet potomku, jejichz navratove hodnoty nebyly zpracovány
	unsigned int children=params.customers+1;
	//PID zombie potomka
	pid_t PID;
	
	//vytvareni potomku
	for(unsigned int i=0; i<params.customers+1; i++)
	{
		//ulozeni PID potomka
		childrenPID[i]=fork();
		
		//chyba pri fork()
		if(childrenPID[i]<0)
		{
			terminate(shrMem, &semArray[NSEM], childrenPID, i+1, shareID);
			fprintf(stderr, "%s", ECODEMSG[EPROC]);
			return EXIT_FAILURE;
		}
		//prvni potomek je holic
		else if(i==0 && childrenPID[i]==0)
		{
			free(childrenPID);
			barber(shrMem, params.chairs, semArray, params.GenB);
			
			if(strcmp(params.output, "-")!=0)
			{
				fclose(stdout);
			}
			
			return EXIT_SUCCESS;
		}
		//ostatni jsou zakaznici
		else if(childrenPID[i]==0)
		{
			free(childrenPID);
			customer(shrMem, i, semArray);

			if(strcmp(params.output, "-")!=0)
			{
				fclose(stdout);
			}
			
			return EXIT_SUCCESS;
		}
		//pokud je zachycena chyba v err
		else if(err!=EOK)
		{
			terminate(shrMem, &semArray[NSEM], childrenPID, i+1, shareID);
			fprintf(stderr, "%s", ECODEMSG[err]);
			return EXIT_FAILURE;
		}
		//pockani mezi generovanim jednotlivych potomku
		else
		{
			usleep((random())%(params.GenC+1));
		}
		
		//odstraneni zombie potomku
		while((PID=waitpid(-1, NULL, WNOHANG))!=0)
		{
			//pokud natala chyba
			if(PID==-1)
			{
				terminate(shrMem, &semArray[NSEM], childrenPID, \
					params.customers+1, shareID);
				
				if(strcmp(params.output, "-")!=0)
				{
					fclose(stdout);
				}	
				
				fprintf(stderr, "%s", ECODEMSG[EPROC]);
				return EXIT_FAILURE;
			}
			//pokud byl uspesne odstranen zombie potomek
			else if(PID>0)
			{
				children--;
			}
		}
	}
	
	//cekani nez potomci zkonci
	for(unsigned int i=0; i<children; i++)
	{
		//chyba pri cekani
		if(waitpid(-1, NULL, 0)==-1)
		{
			terminate(shrMem, &semArray[NSEM], childrenPID, params.customers+1,\
				shareID);
				
			if(strcmp(params.output, "-")!=0)
			{
				fclose(stdout);
			}
	
			fprintf(stderr, "%s", ECODEMSG[EPROC]);
			return EXIT_FAILURE;
		}
		//chyba zachycena v err
		else if(err!=EOK)
		{
			terminate(shrMem, &semArray[NSEM], childrenPID, params.customers+1,\
				shareID);
			fprintf(stderr, "%s", ECODEMSG[err]);
			return EXIT_FAILURE;
		}
	}
	
	//dealokace zdroju
	terminate(shrMem, &semArray[NSEM], childrenPID, 0, shareID);
	
	//pokud byl presmerovam stdout, vrati se do puvodniho stavu
	if(strcmp(params.output, "-")!=0)
	{
		fclose(stdout);
	}
	
	return EXIT_SUCCESS;
}

