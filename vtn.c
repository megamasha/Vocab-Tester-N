#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include <menu.h>
#include <form.h>

#ifdef _WIN32
# define CLEARCOMMAND "cls"
#elif defined __unix__
# define CLEARCOMMAND "clear"
#else
# error "Could not detect OS. Clear screen may not work."
# define CLEARCOMMAND "cls"
#endif 

#define DINPUTFILENAME "vtdb.~sv"
#define DOUTPUTFILENAME "vtdb.~sv"
#define MAXINTVALUE 2147483647
#define MAXTEXTLENGTH 256
#define N2LTONORM 5
#define NORMTON2L 3
#define NORMTOKNOWN 5
#define KNOWNTONORM 2
#define KNOWNTOOLD 3
#define OLDTONORM 1

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct vocab
{
    int index; //identifies the entry in the list, allowing it to be selected by use of a random number
    char * question;//pointer to question text
    char * answer;//pointer to the answer text, which is required for the response to be considered correct
    char * info;//pointer to optional extra text giving advice such as to how to format the response
    char * hint;//pointer to optional text giving a clue to the answer
    int right;//indicates whether counter is counting correct or incorrect responses
    int counter;//counts how many times in a row the answer has been correct/incorrect
    int known;//indicates to what level the vocab is known, and thus to which list it belongs
    struct vocab * next;//pointer to next in list
};

struct listinfo//struct holds head, tail and the number of entries for the n2l, norm, known and old lists
{
    struct vocab * head;
    int entries;
    struct vocab * tail;
};

struct fuzzymatch
{
    struct vocab * entry;
    int score;
};

int maxtextlength = MAXTEXTLENGTH; //allows use of this #define within text strings
FILE * inputfile = NULL;
FILE * outputfile = NULL;
struct listinfo n2l, norm, known, old;
int changedflag = 0;
char currentfilename[MAXTEXTLENGTH+1] = DOUTPUTFILENAME;
int nlines,ncols;
char passingstring[513];

void loaddatabase();//select which database to load and pass it to wgetrecordsfromfile
char * validfilename (char * filename, char * extension);//filename validation
void wgetrecordsfromfile(WINDOW * window,char * inputfilename,char separator);//load a file into memory
char * readtextfromfile(int maxchars,char separator);//get text field from file
int readnumberfromfile(int maxvalue,char separator);//get integer field from file
struct vocab * addtolist(struct vocab * newentry, struct listinfo * list);//add given (already filled in) vocab record to given list
int removefromlist(struct vocab * entry, struct listinfo * list,int freeup);//remove given entry from given list. Also destroy record if freeup is true
int unloaddatabase();//clears all vocab from memory, ready to load another database
void reloaddatabase();//optionally saves and unloads present database before loading another 
void reindex (struct listinfo * list);//necessary to stop gaps in the numbering system, which could cause random vocab selection to fail
void savedatabase();//does what it says on the tin, optionally allows user to give filename, which is passed to wwriteliststofile
int wwriteliststofile(WINDOW * window,char * outputfilename);//output a file from memory to disk
void databasemenu();//provides ability to add entries to database, and edit entries from outside testing mode
struct vocab * createnewvocab();//allows user to create now vocab record within the program
struct vocab * vocabsearch(char * searchstring);//returns a pointer to vocab entry if the question or answer matches given search string
struct vocab * vocabfuzzysearch(char * searchstring);//returns a pointer to a user-selected vocab entry out of a list of up to 10 possible suggestions
int editormenu(struct vocab * entry, int fromtest);//shows menu to edit current entry, fromtest is 1 when run from within the test and 0 when from the menu, returns 1 to be run again, 0 to continue without the menu or -1 to return to the main menu
void testme();//main code for learning vocab, including options menu
char * wgettextfromkeyboard(WINDOW * window, char * target,int maxchars);//set given string (char pointer) from keyboard, allocating memory if necessary
int getyesorno(char * question);//asks for yes or no, returns true (1) if yes
void clrscr();//clears the screen. Now with #ifdef preprocessor script for portability!!
void clearinputbuffer();//clears the input buffer after each request for input, so that the following request is not getting the overflow
float calculatescore(int showstats);//returns overall idea of progress as percentage, displays screenful of stats if 'showstats' is true
void startup();//sets up curses mode, erroring if no can do
void shutdown();//asks about saving if appropriate and exits
void outofmemory();//HowCanThisBe!? Quits...
WINDOW * nicebigwindow();//creates a bordered, blue window, taking up most of the screen, with keypad enabled
WINDOW * innerwindow(WINDOW * outerwindow);//creates an area within another window for purposes of displaying text with a margin
void popupinfo(int colour,char * title,char * message);//pops up a window with the given colour, title and text
void popuperror(char * errormessage);//pops up an error and makes a note in the log
void donothing(),showscore();//does nothing!
void windowtitle(WINDOW * window, char * title);//writes the given string to the given window (top centre)
int textwidth (char * text);//returns the width of a given string (which may include newlines) in chars when displayed without wrapping (for purposes of determining optimum window width)
int textheight (char * text, int width);//returns the height of a given string (which may include newlines) in lines when displayed wrapped to the given width (for purposes of determining optimum window width)

void loaddatabase()//select which database to load
{
    char separator = '~';
    char * tildesep = ".~sv";
    char * commasep = ".csv";
    char * extension = tildesep;
    char * deffilename = DINPUTFILENAME;
    char * inputfilename = (char *)malloc(MAXTEXTLENGTH+1);
    if (!inputfilename) {fprintf(stderr, "Error allocating memory for filename input");exit(1);}
    WINDOW * wbloaddatabase, * wloaddatabase;
    PANEL * ploaddatabase;
    int usingfilename = 1;

    wbloaddatabase = nicebigwindow();
    ploaddatabase = new_panel(wbloaddatabase);
    windowtitle(wbloaddatabase,"Load Database");
    wloaddatabase = innerwindow(wbloaddatabase);

    strcpy(inputfilename,deffilename);
    wprintw(wloaddatabase,"Loading...\nDefault database is: %s\n",inputfilename);
    update_panels();
    doupdate();
    sprintf(passingstring,"Load default database: %s?",inputfilename);
    if (!getyesorno(passingstring))//import user specified database
    {
        wprintw(wloaddatabase,"Not loading default database.\n");
        update_panels();
        doupdate();
        if (getyesorno("Default file type is .~sv. Load .~sv file?")) //import .~sv file
        {
            wprintw(wloaddatabase,"Enter name of .~sv file to load:\n");
        }
        else //alternative options
        {
            wprintw(wloaddatabase,"Not loading .~sv database.\n");
            if (getyesorno("Import .csv file instead?")) //import .csv file
            {
                separator = ',';
                extension = commasep;
                wprintw(wloaddatabase,"Enter name of .csv file to import:\n");
            }
            else //not loading a file
            {
                wprintw(wloaddatabase,"Not importing .csv file.\nNo database file selected. No database loaded!\n");
                usingfilename = 0;
            }
        }
        if (usingfilename) inputfilename=validfilename(wgettextfromkeyboard(wloaddatabase,inputfilename,MAXTEXTLENGTH),extension);
    }
    if (usingfilename)
    {
        wgetrecordsfromfile(wloaddatabase,inputfilename,separator);
        inputfilename=validfilename(inputfilename,".~sv");
        strcpy(currentfilename,inputfilename);
    }
    free(inputfilename);
    getmaxyx(wloaddatabase,nlines,ncols);
    mvwprintw(wloaddatabase,nlines-1,0,"Press any key to continue...");
    wgetch(wloaddatabase);
    delwin(wloaddatabase);
    del_panel(ploaddatabase);
    delwin(wbloaddatabase);
    return;
}

char * validfilename (char * filename, char * extension)//filename validation
{
    int i, j=0, alreadyvalid=1;
    //check filename is longer than the extension
    if (strlen(filename)>strlen(extension))
    {
        //if so, see if string already contains given extension
        for(i=0;i<=strlen(extension);i++)
        {
            if (filename[(strlen(filename))-i]!=extension[(strlen(extension))-i]) alreadyvalid=0;
        }
        if (alreadyvalid) return filename;//is valid filename, return it
    }
    //find first 'dot' or null in string to append file extension (first character can be dot for hidden unix files)
    for (i=1;filename[i]!='.'&&i<strlen(filename);i++);
    //add extension and return result
    while (i<MAXTEXTLENGTH && j<=strlen(extension))
    {
        filename[i]=extension[j];
        i++;j++;
    }
    if (i==MAXTEXTLENGTH) popuperror("Filename reached maximum length including extension, possibly truncated!");
    return filename;
}

void wgetrecordsfromfile(WINDOW * window,char * inputfilename,char separator)
{
    int goodcounter = 0,badcounter = 0;
    struct vocab * newvocab;
    struct listinfo * newvocablist;
    if (!(inputfile = fopen(inputfilename, "r")))
    {
        sprintf(passingstring,"Unable to read input file: '%s'. File does not exist or is in use.",inputfilename);
        popuperror(passingstring);
        wprintw(window,"Loading file Failed.");
    }
    else
    {
        wprintw(window,"Opened input file %s, reading contents...\n",inputfilename);
        while (!feof(inputfile))
        {
            newvocab = (struct vocab *)malloc(sizeof(struct vocab));
            if (!newvocab)
            {
                outofmemory();
            }
            else
            {
                newvocab->question=newvocab->answer=newvocab->info=newvocab->hint=NULL;
                newvocab->question=readtextfromfile(MAXTEXTLENGTH,separator);
                newvocab->answer=readtextfromfile(MAXTEXTLENGTH,separator);
                newvocab->info=readtextfromfile(MAXTEXTLENGTH,separator);
                newvocab->hint=readtextfromfile(MAXTEXTLENGTH,separator);
                newvocab->right=readnumberfromfile(1,separator);
                newvocab->counter=readnumberfromfile(0,separator);
                newvocab->known=readnumberfromfile(3,separator);

                switch (newvocab->known)
                {
                    case 0: newvocablist = &n2l;break;
                    case 1: newvocablist = &norm;break;
                    case 2: newvocablist = &known;break;
                    case 3: newvocablist = &old;break;
                }

                addtolist(newvocab,newvocablist);
                if (newvocab->question==NULL||newvocab->answer==NULL)
                {
                    badcounter++;
                    fprintf(stderr,"Removing faulty vocab record (%d) created at line %i of input file...\n",badcounter,(goodcounter+badcounter));
                    removefromlist(newvocab,newvocablist,1);
                }
                else goodcounter++;
            }
        }
        fclose(inputfile);
        wprintw(window,"...finished.\n%i entries read from %s.\n\n",goodcounter,inputfilename);
        if (badcounter)
        {
            sprintf(passingstring,"%i faulty entries encountered!\n\nIt is HIGHLY recommended you do NOT save back to the original file.\n\nSee error log for details.",badcounter);
            popuperror(passingstring);
        }
    }
    return;
}

char * readtextfromfile(int maxchars,char separator)
{
    int i=0;
    char ch;
    char * target = (char *)malloc(maxchars+1); //allocate memory for new string
    if (!target) outofmemory();

    ch=getc(inputfile);
    if (ch==separator||ch==EOF){free(target);return NULL;}//if field is blank (zero-length), return null pointer (||EOF added because it hangs on blank database)
    while (isspace(ch))
    {
        ch = getc(inputfile);//cycle forward until you reach text
        if (ch == separator||ch=='\n'||ch==EOF) {free(target);return NULL;}//if no text found(reached separator before anything else), return null pointer
    }
    if (ch=='"') //Entry is in quotes (generated by excel when exporting to .csv and field contains a comma)
    {
        ch=getc(inputfile);//move to next character after the quotes
        while (i<(maxchars-1) && ch!='"' && ch!='\n')//stop when you reach the end quotes, end of line, or when text too long
        {
            target[i++]=ch;
            ch = getc(inputfile); //copy name from file to target, one char at a time
        }
        ch=getc(inputfile);//consume separator that follows quotes, so next field does not appear empty (this was a bug... SQEESH!)
    }
    else //entry is not in quotes, so char is currently first letter of string
    {
        while (i<(maxchars-1) && ch!=separator && ch!='\n')//stop when you reach separator, end of line, or when text too long
        {
            target[i++]=ch;
            ch = getc(inputfile); //copy name from file to target, one char at a time
        }
    }
    target[i] = '\0';//terminate string
    return target;
}

int readnumberfromfile (int maxvalue,char separator)
{
    int number, i=0;
    char ch;
    char * buff = (char *)malloc(10+1);//allocate enough space for an 10-digit number and a terminating null
    if (!buff) outofmemory();
    if (!maxvalue) maxvalue=MAXINTVALUE;

    ch=getc(inputfile);
    while (!isdigit(ch))
    {
        if (ch == separator||ch=='\n'||ch==EOF) {fprintf(stderr,"Format error or field missing in file\nExpected number, but found '%c'. Replacing with '0'\n",separator,ch);free(buff);return 0;}//if no number found(reached separator before digit), print error, free buff and return 0
        ch = getc(inputfile);//cycle forward until you reach a digit
    }
    while (i<10 && ch!=separator && ch!='\n')//stop when you reach separator, end of line, or when number too long
    {
        buff[i++]=ch;
        ch = getc(inputfile); //copy number from file to buff, one char at a time
    }
    buff[i] = '\0';//terminate string
    number = atoi(buff)<=maxvalue ? atoi(buff) : maxvalue;//convert string to number and make sure it's in range
    free(buff);
    return number;
}

struct vocab * addtolist(struct vocab * newentry, struct listinfo * list)
{
    if (!list->head)//if head is null, there is no list, so create one
    {
        list->head = list->tail = newentry;//this is the new head and tail
        list->entries = newentry->index = 1;
        newentry->next = NULL;
    }
    else//just appending to the list
    {
        list->tail->next = newentry;//adjust current tail to point to new entry
        list->tail = newentry;//make the new entry the new tail
        newentry->index=++list->entries;
        newentry->next = NULL;
    }
    //give the entry the appropriate 'known' level for this list (for calculating scores, and deducing which list its in without searching)
    if (list==&n2l) newentry->known = 0;
    else if (list==&norm) newentry->known = 1;
    else if (list==&known) newentry->known = 2;
    else if (list==&old) newentry->known = 3;
    else {popuperror("Unable to correctly add vocab entry to list!");return NULL;}

    return newentry;
}

int removefromlist(struct vocab * entry, struct listinfo * list,int freeup)
{
    struct vocab * prev;
    if (list->head == entry) //if entry being deleted is first in the list
    {
        if (list->tail == entry) //if entry is only item in the list
        {
            list->head = list->tail = NULL;
        }
        else //if first in list, but not last
        {
            list->head = entry->next;
        }
    }
    else //entry is not first in list, so set prev to point to previous entry
    {
        prev = list->head;
        while (prev->next!=entry)
        {
            prev=prev->next;
            if (!prev)
            {
                popuperror("Trying to delete an entry from a list it's not in!!\n");
                return 0;
            }
        }
        if (list->tail == entry)//if entry is at the end of the list
        {
            list->tail = prev;
            list->tail->next = NULL;
        }
        else //if entry is somewhere in middle of list
        {
            prev->next=entry->next;
        }
    }//this entry is now not pointed to in any list
    list->entries--;
    /*following line removed because it could theoretically break a list if the entry was removed from a list after it had been added to another
    entry->next = NULL;//and doesn't point to anything either*/
    reindex(list);
    if (freeup) //if freeup is set, this also wipes the record and frees up the memory associated with it
    {
        if(entry->question) free(entry->question);
        if(entry->answer) free(entry->answer);
        if(entry->info) free(entry->info);
        if(entry->hint) free(entry->hint);
        if(entry) free(entry);
    }
    return 1;
}

int unloaddatabase()
{
    int l = 0,counter = 0;
    struct vocab * entry;
    struct listinfo * list; //assigned by switch with l, cycles through all the lists
    for (;l<=3;l++)
    {
        switch (l)
        {
            case 0: {list = &n2l;break;}
            case 1: {list = &norm;break;}
            case 2: {list = &known;break;}
            case 3: {list = &old;break;}
            default: {popuperror("List pointer error!");return 0;}
        }
        while (list->head!=NULL)
        {
            entry = list->head;
            removefromlist(entry,list,1);
            counter++;
        }
    }
    sprintf(passingstring,"Unloaded %i entries from memory.",counter);
    popupinfo(4,"",passingstring);
    return 1;
}

void reindex (struct listinfo * list)
{
    int counter = 1;
    struct vocab * workingentry = list->head;
    while (workingentry)
    {
        workingentry->index = counter++;
        workingentry=workingentry->next;
    }
    if (list->entries!=counter-1) popuperror("Reindexing Error!");
}

void reloaddatabase()//optionally saves and unloads present database before loading another
{
    //printf("Do you want to save your current vocab before loading another database?\nWARNING: Selecting no could lose all data since last save!!\n");
    if (getyesorno("Do you want to save your current vocab before loading another database?\nWARNING: Selecting no could lose all data since last save!!")) savedatabase();
    if (getyesorno("Do you want to unload the current database from memory before loading a new one?\nIf you do not, the current database and the one you are loading will be merged,\nwhich could cause duplicates.")) unloaddatabase();
    loaddatabase();
}

void savedatabase()
{
    char * deffilename = DOUTPUTFILENAME;
    char * outputfilename = (char *)malloc(MAXTEXTLENGTH+1);
    WINDOW * wbsavedatabase, * wsavedatabase;
    PANEL * psavedatabase;

    wbsavedatabase = nicebigwindow();
    psavedatabase = new_panel(wbsavedatabase);
    windowtitle(wbsavedatabase,"Save Database");
    wsavedatabase = innerwindow(wbsavedatabase);

    wprintw(wsavedatabase,"Saving...\n");
    update_panels();
    doupdate();

    if (!outputfilename) outofmemory();
    strcpy(outputfilename,deffilename);
    popupinfo(3,"WARNING:","If you provide a database filename that already exists, that database will be OVERWRITTEN!");
    sprintf(passingstring,"Save to most recently loaded database: %s?",currentfilename);
    if(getyesorno(passingstring))
    {
        strcpy(outputfilename,currentfilename);
    }
    else
    {
        sprintf(passingstring,"Save to default database: %s? (y/n)",outputfilename);
        if (!getyesorno(passingstring))//user specifies filename for database output
        {
            wprintw(wsavedatabase,"A .~sv file will be saved to the filename you provide.\nPlease enter a name for the .~sv file:\n");
            outputfilename=validfilename(wgettextfromkeyboard(wsavedatabase,outputfilename,MAXTEXTLENGTH),".~sv");
        }
    }
    if (!wwriteliststofile(wsavedatabase,outputfilename)) popuperror("Error while saving!!"); //print error message if wwriteliststofile returned 0
    else changedflag = 0;
    free(outputfilename);
    getmaxyx(wsavedatabase,nlines,ncols);
    mvwprintw(wsavedatabase,nlines-1,0,"Press any key to continue...");
    wgetch(wsavedatabase);
    delwin(wsavedatabase);
    del_panel(psavedatabase);
    delwin(wbsavedatabase);
    return;
}

int wwriteliststofile(WINDOW * window,char * outputfilename)
{
    int i,counter=0;
    struct listinfo * list;
    struct vocab * entry;
    if (!(outputfile = fopen(outputfilename, "w")))
    {
        popuperror("Error accessing output file!");
        return 0;
    }
    else
    {
        wprintw(window,"Saving...\n");
        for (i=0;i<=3;i++)
        {
            switch (i)
            {
                case 0: list = &n2l;break;
                case 1: list = &norm;break;
                case 2: list = &known;break;
                case 3: list = &old;break;
                default: popuperror("Loop Error!");break;
            }
            entry=list->head;
            while (entry!=NULL)
            {
                if (counter) fprintf(outputfile,"\n");
                fprintf(outputfile,"%s~%s~",entry->question,entry->answer);
                if (entry->info) fprintf(outputfile,"%s",entry->info);
                fprintf(outputfile,"~");
                if (entry->hint) fprintf(outputfile,"%s",entry->hint);
                fprintf(outputfile,"~%i~%i~%i",entry->right,entry->counter,i);
                entry=entry->next;
                counter++;
            }
        }
        fclose(outputfile);
        wprintw(window,"...finished. %i entries saved to file: %s\n",counter,outputfilename);
        return 1;
    }
}

void databasemenu()//provides ability to add entries to database, and edit entries from outside testing mode
{
    WINDOW * wbdatabasemenu, * wdatabasemenu;
    PANEL * pdatabasemenu;
    ITEM ** databasemenuitems;
    MENU * databasemenu;
    struct vocab * entry;
    int menuchoice = '\n';
    int menuresult=1;
    char * searchstring = (char *)malloc(MAXTEXTLENGTH+1);
    if (!searchstring) popuperror("Unable to allocate memory! for search string.");
    
    char * databasemenuchoices[][2] = //strings for menu
    {
        {"a:","Add Vocab"},
        {"t:","Edit or delete vocab"},
        {"x:","Exit to main menu"}
    };
    char databasemenupointers[] =
    {
        'a',
        'e',
        'x'
    };
    
    ITEM * ITEMselected; //this will point to selected item
    char * pselected; //this will point to the char attached to selected item
    
    int i,numberofchoices = ARRAY_SIZE(databasemenuchoices);    
    if(!(databasemenuitems = (ITEM**)calloc(numberofchoices+1,sizeof(ITEM*)))) outofmemory();
    for(i=0;i < numberofchoices;i++)
    {
        databasemenuitems[i] = new_item(databasemenuchoices[i][0], databasemenuchoices[i][1]);
        set_item_userptr (databasemenuitems[i],&databasemenupointers[i]);
    }
    databasemenuitems[numberofchoices] = (ITEM *)NULL;

    wbdatabasemenu = nicebigwindow();
    pdatabasemenu = new_panel(wbdatabasemenu);
    wdatabasemenu = innerwindow(wbdatabasemenu);
    windowtitle(wbdatabasemenu,"Database Management Menu");
    databasemenu = new_menu(databasemenuitems);
    set_menu_win(databasemenu,wdatabasemenu);
    set_menu_back(databasemenu,COLOR_PAIR(1));
    menu_opts_off(databasemenu,O_NONCYCLIC);
    post_menu(databasemenu);
    update_panels();
    doupdate();

    while (menuchoice!='x')
    {
        entry = NULL;
        menuchoice = wgetch(wdatabasemenu);
        if (menuchoice == 10)
        {
            ITEMselected = current_item(databasemenu);
            pselected = item_userptr(ITEMselected);
            menuchoice = *pselected;
        }
        switch (menuchoice)
        {
            case KEY_UP: menu_driver(databasemenu,REQ_UP_ITEM);
                        break;
            case KEY_DOWN: menu_driver(databasemenu,REQ_DOWN_ITEM);
                        break;
            case 'a': if (createnewvocab()) {changedflag = 1;popupinfo(4,"Success!","Vocab successfully added.");}
                      else popuperror("Vocab creation failed!");
                      break;
            case 'e': changedflag = 1;wmove(wdatabasemenu,4,0);wprintw(wdatabasemenu,"Entry to edit or delete:\n");clrtoeol();
                searchstring=wgettextfromkeyboard(wdatabasemenu,searchstring,MAXTEXTLENGTH);
                if (searchstring) entry = vocabsearch(searchstring);
                if (entry)
                {
                    menuresult=1;
                    while (menuresult==1)
                    {
                        menuresult = editormenu(entry,0);
                    }
                    if (menuresult==-1) goto cleanup;
                }
                else popupinfo(2,"","No entry selected");
                break;
            case 'x': break;
        }
    }
    cleanup:
    free(searchstring);
    unpost_menu(databasemenu);
    free_menu(databasemenu);
    for (i=0;i<numberofchoices;i++)
    free_item(databasemenuitems[i]);
    del_panel(pdatabasemenu);
    delwin(wdatabasemenu);
    delwin(wbdatabasemenu);
}

struct vocab * createnewvocab()//allows user to create now vocab record within the program
{
    WINDOW * wbcreatevocab, * wcreatevocab;
    PANEL * pcreatevocab;
    struct vocab * newvocab;
    struct listinfo * newvocablist = &norm;
    
    wbcreatevocab = nicebigwindow();
    pcreatevocab = new_panel(wbcreatevocab);
    wcreatevocab = innerwindow(wbcreatevocab);
    windowtitle(wbcreatevocab,"Create new vocab");
    
    newvocab = (struct vocab *)malloc(sizeof(struct vocab));
    if (!newvocab) outofmemory();
    else
    {
        newvocab->question=newvocab->answer=newvocab->info=newvocab->hint=NULL;
        wprintw(wcreatevocab,"Enter question text for this entry (max %i chars):\n",maxtextlength);
        newvocab->question=wgettextfromkeyboard(wcreatevocab,newvocab->question,MAXTEXTLENGTH);
        wprintw(wcreatevocab,"Enter answer text for this entry (max %i chars):\n",maxtextlength);
        newvocab->answer=wgettextfromkeyboard(wcreatevocab,newvocab->answer,MAXTEXTLENGTH);
        if (getyesorno("Would you like to add additional info for this entry?"))
        {
            wprintw(wcreatevocab,"Enter info for this entry (max %i chars):\n",maxtextlength);
            newvocab->info=wgettextfromkeyboard(wcreatevocab,newvocab->info,MAXTEXTLENGTH);
        }
        else
        {
            newvocab->info=NULL;
            wprintw(wcreatevocab,"No info added\n");
        }
        if (getyesorno("Would you like to add a hint to help you remember this entry?"))
        {
            wprintw(wcreatevocab,"Enter hint for this entry (max %i chars):\n",maxtextlength);
            newvocab->hint=wgettextfromkeyboard(wcreatevocab,newvocab->hint,MAXTEXTLENGTH);
        }
        else
        {
            newvocab->hint=NULL;
            wprintw(wcreatevocab,"No hint added\n");
        }
        newvocab->right=0;
        newvocab->counter=0;
        newvocab->known=1;

        if (newvocab->question==NULL||newvocab->answer==NULL) //minimal validation for valid record
        {
            popuperror("Question and/or answer are blank!");
            del_panel(pcreatevocab);
            delwin(wcreatevocab);
            delwin(wbcreatevocab);
            return NULL;
        }

        if (addtolist(newvocab,newvocablist))
        {
            del_panel(pcreatevocab);
            delwin(wcreatevocab);
            delwin(wbcreatevocab);
            return newvocab;
        }
        else
        {
            del_panel(pcreatevocab);
            delwin(wcreatevocab);
            delwin(wbcreatevocab);
            return NULL;
        }
    }
}

struct vocab * vocabsearch(char * searchstring)//returns a pointer to vocab entry if the question or answer matches given search string
{
    struct vocab * entry = NULL, * match = NULL;
    struct listinfo * list = NULL;
    int i,numberofmatches=0;
    for (i=0;i<=3;i++)
    {
        switch (i)
        {
            case 0: list = &n2l;break;
            case 1: list = &norm;break;
            case 2: list = &known;break;
            case 3: list = &old;break;
            default: popuperror("Loop Error!");break;
        }
        entry=list->head;
        while (entry!=NULL)
        {
            if (!strcmp(entry->question,searchstring))
            {
                match = entry;
                numberofmatches++;
            }
            entry=entry->next;
        }
    }
    for (i=0;i<=3;i++)
    {
        switch (i)
        {
            case 0: list = &n2l;break;
            case 1: list = &norm;break;
            case 2: list = &known;break;
            case 3: list = &old;break;
            default: popuperror("Loop Error!");break;
        }
        entry=list->head;
        while (entry!=NULL)
        {
            if (!strcmp(entry->answer,searchstring))
            {
                match = entry;
                numberofmatches++;
            }
            entry=entry->next;
        }
    }
    if (numberofmatches == 1) return match;
    else if (numberofmatches)
    {
        if (getyesorno("More than one match found. Show best matches?")) return vocabfuzzysearch(searchstring);
    }
    else
    {
        if (getyesorno("No exact matches found. Perform fuzzy search?")) return vocabfuzzysearch(searchstring);
    }
    return NULL;
}

//FISH! TODO Continue Converting from here

struct vocab * vocabfuzzysearch(char * searchstring)//returns a pointer to vocab entry that has the largest number of innitial, non case-sensitive characters
{
    WINDOW * wbfuzzysearch, * wfuzzysearch;
    PANEL * pfuzzysearch;
    ITEM ** fuzzysearchmenuitems;
    ITEM * ITEMselected;
    MENU * fuzzysearchmenu;
    struct vocab * entry, * returnvalue = NULL, * pselected;
    struct fuzzymatch matches[10];
    struct fuzzymatch * worstmatch = &matches[0];
    struct listinfo * list;
    int i,j,matchescounter=0,currentscore=0;
    int substringlength[2];//FISH! TODO Can the separate while loops below be combined using 'substringlength++'?
    if (!(fuzzysearchmenuitems=(ITEM**)calloc(11,sizeof(ITEM*)))) outofmemory();

    //innitialise all fuzzymatches.
    for(i=0;i<10;i++) {matches[i].entry = NULL;matches[i].score = 0;}

    //cycle through all entries...
    for (i=0;i<=3;i++)
    {
        switch (i)
        {
            case 0: list = &n2l;break;
            case 1: list = &norm;break;
            case 2: list = &known;break;
            case 3: list = &old;break;
            default: popuperror("Loop Error!");break;
        }
        entry=list->head;
        while (entry!=NULL)
        {
            //...giving them a score based on...
            currentscore=0;
            //...containing the search string (strstr, +10 points)...
            if((!strcmp(searchstring,entry->question))||(!strcmp(searchstring,entry->answer))) currentscore += 10;
            //...two extra points for each sequential letter (starting from the beginning of searchstring) that is contained IN ORDER in the entry (strncmp)...
            substringlength[0]=strlen(searchstring);//check question string
            while (strncmp(searchstring,entry->question,(size_t)substringlength[0]))
            {
                substringlength[0]--;
                if(!substringlength[0])break;
            }
            substringlength[1]=strlen(searchstring);//check answer string;
            while (strncmp(searchstring,entry->question,(size_t)substringlength[1]))
            {
                substringlength[1]--;
                if(!substringlength[1])break;
            }
            currentscore= (substringlength[0]>substringlength[1]) ? (currentscore+=(2*substringlength[0])) : (currentscore+=(2*substringlength[1]));//increment currentscore by two times the greater of the two substringlengths
            //...and an extra point for each sequential letter (once again from the beginning of searchstring) that appears REGARDLESS OF POSITION in the entry (strspn).
            currentscore+=strspn(searchstring,entry->question);
            currentscore+=strspn(searchstring,entry->answer);

            //if the score is greater than the lowest score currently in the matches[] array, overwrite the lowest scored match in the array with a pointer to this.
            if (currentscore>worstmatch->score)
            {
                worstmatch->score=currentscore;
                worstmatch->entry=entry;
                //set worstmatch to match with new worst score
                for(j=0;j<10;j++) if(worstmatch->score > matches[j].score) worstmatch = &matches[j];
            }
            entry=entry->next;
        }
    }
    //display numbered list of questions and answers for these matches, asking the user to decide which they'd like to select (0 for none)
    wbfuzzysearch=nicebigwindow();
    windowtitle(wbfuzzysearch,"Fuzzy Search");
    pfuzzysearch = new_panel(wbfuzzysearch);
    wfuzzysearch=innerwindow(wbfuzzysearch);
    
    i=0;
    while (i<10)
    {
        if (matches[i].entry)
        {
            fuzzysearchmenuitems[i]=new_item(matches[i].entry->question,matches[i].entry->answer);
            set_item_userptr(fuzzysearchmenuitems[i],matches[i].entry);
            i++; continue;
        }
        else break;
    }
    fuzzysearchmenuitems[i]=NULL;
    fuzzysearchmenu=new_menu(fuzzysearchmenuitems);
    set_menu_win(fuzzysearchmenu,wfuzzysearch);
    set_menu_sub(fuzzysearchmenu,derwin(wfuzzysearch,0,0,0,0));
    set_menu_back(fuzzysearchmenu,COLOR_PAIR(1));
    menu_opts_off(fuzzysearchmenu,O_NONCYCLIC);
    set_menu_format(fuzzysearchmenu, 10, 1);
    post_menu(fuzzysearchmenu);
    update_panels();
    doupdate();
    while (1)
    {
        i=wgetch(wfuzzysearch);
        switch (i)
        {
            case 10: ITEMselected = current_item(fuzzysearchmenu);
                    returnvalue=item_userptr(ITEMselected);
                    goto cleanup;
                    break;
            case KEY_UP: menu_driver(fuzzysearchmenu,REQ_UP_ITEM); break;
            case KEY_DOWN: menu_driver(fuzzysearchmenu,REQ_DOWN_ITEM); break;
            default: goto cleanup;break;
        }
    }
    cleanup:
    unpost_menu(fuzzysearchmenu);
    free_menu(fuzzysearchmenu);
    for (i=0;i<10;i++)
    if (fuzzysearchmenuitems[i]) free_item(fuzzysearchmenuitems[i]);
    del_panel(pfuzzysearch);
    delwin(wfuzzysearch);
    delwin(wbfuzzysearch);
    return returnvalue;
}

int editormenu(struct vocab * entry, int fromtest)//shows menu to edit current entry, fromtest is 1 when run from within the test and 0 when from the menu, returns 1 to show menu again, 0 to close the menu or -1 to return to the main menu
{
    struct listinfo * list;
    char optionsmenuchoice = '\n';
    int showagain = 1;
    changedflag = 1;
    if (entry==NULL) {fprintf(stderr,"Somehow received blank entry! Fix me.\n");return 0;}
    clrscr();
    if (entry->known==0) list = &n2l;
    else if (entry->known==1) list = &norm;
    else if (entry->known==2) list = &known;
    else if (entry->known==3) list = &old;
    else {fprintf(stderr,"Unable to deduce list!!");exit(1);}
    printf("Current Entry:\n\nQuestion: %s\nAnswer: '%s'\n",entry->question,entry->answer);
    if (entry->info) printf("Info: %s\n",entry->info);else printf("No info.\n");
    if (entry->hint) printf("Hint: %s\n\n",entry->hint);else printf("No hint.\n\n");
    printf("Options Menu:\n\nType q to modify the question phrase displayed for translation.\nType a to change the answer phrase you must provide.\nType i to add/modify additional info for this entry.\nType h to add/modify the hint for this entry.\nType p to mark this entry as high priority to learn.\nType d to delete this entry from the database.\n");
    if (fromtest) printf("Type t to close this menu and continue testing.\n");
    else printf("Type r to return to the database management menu\n");
    printf("Type x to ");
    if (fromtest) printf("end testing and ");
    printf("return to the main menu.\n\n");
    optionsmenuchoice=getchar();
    clearinputbuffer();
    switch (optionsmenuchoice)
    {
        case 'q': printf("Enter new question text for this entry (max %i chars):\n",maxtextlength);
        entry->question=wgettextfromkeyboard(/*FISH!*/stdscr,entry->question,MAXTEXTLENGTH);
        break;
        case 'a': printf("Enter new answer text for this entry (max %i chars):\n",maxtextlength);
        entry->answer=wgettextfromkeyboard(/*FISH!*/stdscr,entry->answer,MAXTEXTLENGTH);
        break;
        case 'i': printf("Enter new info for this entry (max %i chars):\n",maxtextlength);
        entry->info=wgettextfromkeyboard(/*FISH!*/stdscr,entry->info,MAXTEXTLENGTH);
        break;
        case 'h': printf("Enter new hint for this entry (max %i chars):\n",maxtextlength);
        entry->hint=wgettextfromkeyboard(/*FISH!*/stdscr,entry->hint,MAXTEXTLENGTH);
        break;
        case 'p': if(list==&n2l)printf("Already marked as priority!\n"); //was using = instead of == in if condition, thank you very much gcc compiler output :-)
                  else
                  {
                      removefromlist(entry,list,0);
                      entry->counter = 0;
                      list=&n2l;
                      addtolist(entry,list);
                      printf("This entry will be brought up more often\n");
                  }
                  break;
        case 'd': printf("Are you sure you want to delete this entry?\nOnce you save, this will be permanent!(y/n)");
                  if (getyesorno("FISH!")) {removefromlist(entry,list,1);printf("Entry deleted!\n");return 0;}
                  else printf("Entry was NOT deleted.\n");
                  break;
        case 'x': return -1;
        break;
        case 't': if (fromtest) return 0;
                  else printf("You are not currently testing.\nReturn to the main menu and select 'Test me!'.\n");
        break;
        case 'r': if (fromtest) printf("Database management is not available from testing mode.\nReturn to the main menu and select 'Manage database'.\n");
                  else return 0;
        break;
        default: printf("Invalid choice.\n");
    }
    printf("Select again from the options menu? (y/n)");
    if (getyesorno("FISH!")) return 1;
    else
    {
        if (fromtest) printf("Continue testing?(y/n)"); else printf ("Return to database management menu?(y/n)");
        if (getyesorno("FISH!")) return 0; else return -1;
    }
}

void testme()
{
    int list_selector=0, entry_selector=0, bringupmenu = 0, testagain=1, menuresult=0, usedhint=0;
    int n2l_flag=0; //Prevents 'need to learn's coming up twice in a row
    struct listinfo * currentlist = NULL;
    struct vocab * currententry = NULL;
    char testmenuchoice = '\n';
    char * youranswer = (char *)malloc(MAXTEXTLENGTH+1);
    if (!youranswer) {printf("Memory allocation error!\n");return;}

    while (testagain)
    {
        clrscr();

        //select a list at random, using the percentage probabilities in the if statements.
        list_selector = (rand() % 100)+1;
        if (list_selector>100) {fprintf(stderr,"Problem with random number generator. Fix it!");exit(1);}
        else if (list_selector==100) currentlist = &old;
        else if (list_selector>94) currentlist = &known;
        else if (list_selector>32) {n2l_flag=0;currentlist=&norm;} //use norm list and cancel n2l flag (not cancelled with other lists)
        else if (list_selector<33) currentlist = &n2l;

        //do a little control over random selection
        if (currentlist==&n2l && n2l_flag)
        {
	    currentlist=&norm;
	    n2l_flag=0; //if n2l list was used last time as well (flag is set), use entry from the norm list instead
	}
        if (currentlist==&n2l) n2l_flag = 1; //is using n2l this time, set flag so it won't be used next time as well

        if (currentlist->entries==0) currentlist = &norm;//if current list is empty, default to normal list
        if (currentlist->entries==0 && !n2l_flag) currentlist = &n2l;//if normal list is empty, try n2l list if it wasn't used last time
        if (currentlist->entries==0 && list_selector%10==5) currentlist = &old;//if list is still empty, in 10% of cases try old list
        if (currentlist->entries==0) currentlist = &known;//in the other 90% of cases, or if old is empty, use the known list
        if (currentlist->entries==0) currentlist = &old;//if known list is empty, try the old list
        if (currentlist->entries==0) {currentlist = &n2l;n2l_flag=1;}//if old list is empty, use n2l list EVEN if it was used last time
        if (currentlist->entries==0) {printf("No entries in list!\n\n");free(youranswer);clearinputbuffer();return;} //if list is STILL empty, abort

        //we now have the desired list of words with at least one entry, let's select an entry at random from this list
        entry_selector = (rand() % currentlist->entries)+1;
        currententry = currentlist->head;
        while (currententry->index!=entry_selector)
        {
            currententry = currententry->next;//move through list until index matches the random number
            if (currententry==NULL) {printf("Indexing error!\nCurrent list selector: %i, entries: %i, entry selector: %i\n",list_selector,currentlist->entries,entry_selector);free(youranswer);return;}//in case not found in list
        }

        changedflag = 1;
        printf("Translate the following:\n\n\t%s\n\n",currententry->question);
        if (!currententry->info) printf("There is no additional information for this entry.\n");
        else printf("Useful Info: %s\n\n",currententry->info);
        if (!currententry->hint) printf("There is no hint available for this entry.\n");
        else printf("There is a hint available for this entry. Type 'h' to view it.\nIf you view the hint, correct answers will not improve your score.\n\n");
        printf("Your Translation");
        if (currententry->hint) printf(" (or 'h' for hint)");
        printf(":\n\n\t");
        wgettextfromkeyboard(/*FISH!*/stdscr,youranswer,MAXTEXTLENGTH);

        if (currententry->hint) //if there's a hint available...
        {
            usedhint=0;
            if (!strcmp(youranswer,"h")) //...if it is used...
            {
                usedhint = 1; //...mark as used
                printf("\nHINT: %s\n\nYour Translation:\n\n\t",currententry->hint); //display hint
                wgettextfromkeyboard(/*FISH!*/stdscr,youranswer,MAXTEXTLENGTH); //prompt for answer
            }
        }

        if (!strcmp(youranswer,currententry->answer))//if you're right
        {
            if(usedhint)
            {
                printf("\nWell done. See if you can remember without the hint next time...\n");
                
                currententry->right = currententry->counter = 1;
                if (currentlist==&old)
                {
                    removefromlist(currententry,currentlist,0);
                    printf("It will be brought up a couple more times to help you remember it.\n");
                    addtolist(currententry,&known);
                }
            }
            else
            {
                printf("Yay!\n");

                if(currententry->right) currententry->counter++;
                else currententry->right = currententry->counter = 1;
                if (currententry->counter>2) printf("You answered correctly the last %i times in a row!\n",currententry->counter);
            }

            //make comments based on how well it's known, and move to a higher list if appropriate
            if (currentlist==&n2l && currententry->counter>=N2LTONORM)
            {
                removefromlist(currententry,currentlist,0);
                printf("Looks like you know this one a little better now!\nIt will be brought up less frequently.\n");
                currententry->counter = 1;
                addtolist(currententry,&norm);
            }
            if (currentlist==&norm && currententry->counter>=NORMTOKNOWN)
            {
                removefromlist(currententry,currentlist,0);
                printf("Looks like you know this one now!\nIt will be brought up much less frequently.\n");
                currententry->counter = 1;
                addtolist(currententry,&known);
            }
            if (currentlist==&known && currententry->counter>=KNOWNTOOLD)
            {
                removefromlist(currententry,currentlist,0);
                printf("OK! So this one's well-learnt.\nIt probably won't be brought up much any more.\n");
                currententry->counter = 1;
                addtolist(currententry,&old);
            }
        }
    
        else //if you're wrong
        {
            printf("\nSorry, the correct answer is:\n\n\t%s\n\n",currententry->answer);
        
            if(!currententry->right) currententry->counter++;
            else {currententry->right = 0; currententry->counter = 1;}
            if (currententry->counter>1) printf("You've got this one wrong the last %i times.\n",currententry->counter);
            if (currentlist==&norm && currententry->counter>=NORMTON2L)
            {
                removefromlist(currententry,currentlist,0);
                printf("This one could do with some learning...\n");
                currententry->counter = 1;
                addtolist(currententry,&n2l);
            }
            if (currentlist==&known && currententry->counter>=KNOWNTONORM)
            {
                removefromlist(currententry,currentlist,0);
                printf("OK, perhaps you don't know this one as well as you once did...\n");
                currententry->counter = 1;
                addtolist(currententry,&norm);
            }
            if (currentlist==&old && currententry->counter>=OLDTONORM)
            {
                removefromlist(currententry,currentlist,0);
                printf("This old one caught you out, huh? It will be brought up a few more times to help you remember it.\n");
                currententry->counter = 1;
                addtolist(currententry,&norm);
            }
        }

        printf("Your score is now %.1f%%.\n",calculatescore(0));
        printf("Type 'o' for options or strike enter for another question\n");
        testmenuchoice = getchar();
        if (tolower(testmenuchoice)=='o') bringupmenu = 1;
        if (testmenuchoice!='\n') clearinputbuffer();
        while (bringupmenu)
        {
            menuresult = editormenu(currententry,1);
            switch (menuresult)
            {
                case -1: bringupmenu=testagain=0;break;
                case  0: bringupmenu=0;break;
                default: continue;
            }
        }
        clrscr();
    }
    free(youranswer);
    return;
}

char * wgettextfromkeyboard(WINDOW * window, char * target,int maxchars)
{
    int i =0;
    int memoryallocated_flag =0; //to avoid freeing memory allocated outside function, pointed out by stackoverflow.com/users/688213/mrab
    char ch;
    if (!target)//if no memory already allocated (pointer is NULL), do it now
    {
        memoryallocated_flag=1;
        target=(char *)malloc(maxchars+1);
        if (!target) {printf("Memory allocation failed!");return NULL;}//return null if failed
    }
    echo();
    wgetnstr(window,target,maxchars);
    noecho();
    return target;
}

int getyesorno(char * question)
{
    WINDOW * wbgetyesorno, * wgetyesorno;
    PANEL * pgetyesorno;
    MENU* getyesornomenu;
    ITEM ** getyesornoitems;//this pointer will be passed to the menu
    char * getyesornochoices[] = //strings for menu
    {
        "[ No ]",
        "[ Yes ]"
    };
    int getyesornoreturnvalues[] = { 0 , 1 };

    ITEM * ITEMselected; //this will point to selected item
    int * pselected; //this will point to the function attached to selected item

    int i, questionwidth, questionheight, numberofchoices = ARRAY_SIZE(getyesornochoices);
    int returnvalue = 0, loopflag;

    questionwidth=textwidth(question);
    if (questionwidth<17) questionwidth=17;
    else
    {
        getmaxyx(stdscr,nlines,ncols);
        if (questionwidth>ncols-16)questionwidth=ncols-16;
    }
    questionheight=textheight(question,questionwidth);
    wbgetyesorno = newwin(questionheight+7,questionwidth+8,(nlines-(questionheight+7))/2,(ncols-(questionwidth+8))/2);
    if(!wbgetyesorno)outofmemory();
    pgetyesorno = new_panel(wbgetyesorno);
    wattrset(wbgetyesorno,COLOR_PAIR(2));
    werase(wbgetyesorno);
    wbkgd(wbgetyesorno,COLOR_PAIR(2));
    box(wbgetyesorno,0,0);
    windowtitle(wbgetyesorno,"Yes or No Question:");
    getmaxyx(wbgetyesorno,nlines,ncols);
    wgetyesorno = derwin(wbgetyesorno,nlines-4,ncols-8,2,4);
    if (!wgetyesorno) outofmemory();
    keypad(wgetyesorno,TRUE);

    if(!(getyesornoitems = (ITEM**)calloc(numberofchoices+1,sizeof(ITEM*)))) outofmemory();
    for(i=0;i < numberofchoices;i++)
    {
        getyesornoitems[i] = new_item(getyesornochoices[i], getyesornochoices[i]);
        set_item_userptr (getyesornoitems[i],&getyesornoreturnvalues[i]);
    }
    getyesornoitems[numberofchoices] = (ITEM *)NULL;
    
    getyesornomenu = new_menu(getyesornoitems);
    set_menu_win(getyesornomenu,wgetyesorno);
    getmaxyx(wgetyesorno,nlines,ncols);
    set_menu_sub(getyesornomenu,derwin(wgetyesorno,1,17,nlines-1,(ncols-17)/2));
    set_menu_back(getyesornomenu,COLOR_PAIR(2));
    menu_opts_off(getyesornomenu, O_SHOWDESC);
    set_menu_format(getyesornomenu, 1, 2);

    wprintw(wgetyesorno,question);
    post_menu(getyesornomenu);
    update_panels();
    doupdate();

    loopflag = 1;
    int yesorno = '\n';
    while (loopflag)
    {
        yesorno=wgetch(wgetyesorno);
        switch (tolower(yesorno))
        {
            case 'y': returnvalue = 1;loopflag = 0;break;
            case 'n': returnvalue = 0;loopflag = 0;break;
            case KEY_RIGHT: menu_driver(getyesornomenu,REQ_RIGHT_ITEM); break;
            case KEY_LEFT: menu_driver(getyesornomenu,REQ_LEFT_ITEM); break;
            case 10:    ITEMselected = current_item(getyesornomenu);
                        pselected = (int *)item_userptr(ITEMselected);
                        returnvalue = *pselected;
                        loopflag = 0;
                        break;
        }
    }
    unpost_menu(getyesornomenu);
    free_menu(getyesornomenu);
    for (i=0;i<numberofchoices;i++);
    delwin(wgetyesorno);
    del_panel(pgetyesorno);
    delwin(wbgetyesorno);
    update_panels();
    doupdate();
    return returnvalue;
}

void clrscr()
{
    system(CLEARCOMMAND);
}

void clearinputbuffer()
{
    char tempchar;
    if (getchar()=='\n') return;
    else while (1)
    {
        tempchar = getchar();
        if (tempchar=='\n') break;
    }
    return;
}

float calculatescore(int showstats)//returns overall idea of progress as percentage, displays screenful of stats if 'showstats' is true
{
    struct vocab * entry,* bestrunentry = NULL,* worstrunentry = NULL;
    struct listinfo * list;
    int i,count=0,knowntotal=0,infos=0,hints=0,untested=0,rights=0,wrongs=0,bestrun=0,worstrun=0;
    float score;
    for (i = 0;i<=3;i++)
    {
        switch (i)
        {
            case 0: list = &n2l;break;
            case 1: list = &norm;break;
            case 2: list = &known;break;
            case 3: list = &old;break;
            default: fprintf(stderr,"Loop Error!\n");break;
        }
        entry = list->head;
        while (entry!=NULL)
        {
            count++;
            knowntotal += entry->known;
            if (showstats)
            {
                if (entry->info) infos++;
                if (entry->hint) hints++;
                if (entry->counter==0) untested++;
                else if (entry->right) rights++;
                else wrongs++;
                if (entry->right && entry->counter > bestrun)
                {
                    bestrun = entry->counter;
                    bestrunentry = entry;
                }
                if ((!(entry->right)) && entry->counter > worstrun)
                {
                    worstrun = entry->counter;
                    worstrunentry = entry;
                }
            }
            entry=entry->next;
        }
    }
    if (!count) {printf("No entries in list!");clearinputbuffer();return 0;}
    score = ((float)knowntotal / (3*(float)count))*100;
    if (showstats)
    {
        clrscr();
        printf("Your current stats:\n\nYour current score: %.1f%%\n\nThere are presently %i entries loaded.\n\n",score,count);
        if (untested) printf("%d of these you've never been tested on.\n",untested);
        else printf("You've been tested on all of them at least once.\n");
        printf("%i of these you got RIGHT the last time they came up.\n",rights);
        printf("%i of these you got WRONG the last time they came up.\n\n",wrongs);
        printf("%i loaded entries have additional info.\n",infos);
        printf("%i loaded entries have an associated hint.\n\n",hints);
        if (bestrun) printf("Your longest run of consecutive right answers is currently '%s', which you got right the last %i times.\n\n",bestrunentry->question,bestrun);
        if (worstrun) printf("Your longest run of consecutive wrong answers is currently '%s', which you got wrong the last %i times.\n\n",worstrunentry->question,worstrun);
        clearinputbuffer();
    }
    return score;
}

void startup()//sets up curses mode, erroring if no can do
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);//FISH! Want this for other windows?
    if (has_colors()==FALSE) {printw("Sorry, your terminal doesn't support the colour features\nof this version of the vocab tester.\nPlease use Version N, which uses plain, uncoloured text.\nPress any key to exit (where's the 'any' key?).");refresh();getch();endwin();exit(EXIT_FAILURE);}
    start_color();
    init_pair(1,COLOR_WHITE,COLOR_BLUE);
    init_pair(2,COLOR_BLACK,COLOR_WHITE);
    init_pair(3,COLOR_WHITE,COLOR_RED);
    init_pair(4,COLOR_WHITE,COLOR_GREEN);

    freopen ("errorlog.txt","a",stderr);

    srand((unsigned)time(NULL));

    n2l.entries = norm.entries = known.entries = old.entries = 0;
}

void shutdown()//asks about saving if appropriate and exits
{
    if (changedflag)
    {
        if (getyesorno("Your database has changed (or you have given more answers) since you last saved.\nIf you continue without saving, these changes will be lost!\n\nSave now?"))
            savedatabase();
    }
    erase();
    printw("Bye for now!\n\nPress any key to exit. (Where's the 'any' key?)");
    refresh();
    getch();
    endwin();
    exit(EXIT_SUCCESS);
}

void outofmemory()//HowCanThisBe!? Quits...
{
    erase();
    fprintf(stderr,"Out of memory.\n");
    printw("HowCanThisBe!? Out of memory!\nCheck errorlog.txt for other errors\n\nQuitting... (press enter)\n");
    refresh();
    getch();
    endwin();
    exit(EXIT_FAILURE);
}

WINDOW * nicebigwindow()//creates a bordered, blue window, taking up most of the screen, with keypad enabled
{
    WINDOW * wtemp;
    getmaxyx(stdscr,nlines,ncols);
    wtemp = newwin(nlines-4,ncols-8,2,4);
    if(!wtemp)outofmemory();
    wattrset(wtemp,COLOR_PAIR(1));
    wbkgd(wtemp,COLOR_PAIR(1));
    werase(wtemp);
    box(wtemp,0,0);
    keypad(wtemp,TRUE);
    return wtemp;
}

void popupinfo(int colour,char * title,char * message)//pops up a window with the given colour, title and text
{
    WINDOW * wbpopup, * wpopup;
    PANEL * ppopup;
    int width, height;
    
    width=textwidth(message);
    getmaxyx(stdscr,nlines,ncols);
    if (width>ncols-16)width=ncols-16;
    height=textheight(message,width)+4;
    width+=8;
    wbpopup = newwin(height,width,(nlines-height)/2,(ncols-width)/2);
    if(!wpopup)outofmemory();
    ppopup = new_panel(wbpopup);
    wattrset(wbpopup,COLOR_PAIR(colour));
    werase(wbpopup);
    wbkgd(wbpopup,COLOR_PAIR(colour));
    box(wbpopup,0,0);
    windowtitle(wbpopup,title);
    wpopup = innerwindow(wbpopup);
    
    wprintw(wpopup,message);
    update_panels();
    doupdate();
    wgetch(wpopup);
    
    delwin(wpopup);
    del_panel(ppopup);
    delwin(wbpopup);
    update_panels();
    doupdate();
}


void popuperror(char * errormessage)//pops up an error and makes a note in the log
{
    WINDOW * wberror, * werror;
    PANEL * perror;
    int errorwidth, errorheight;

    fprintf(stderr,"%s\n",errormessage);
    errorwidth=textwidth(errormessage);
    getmaxyx(stdscr,nlines,ncols);
    if (errorwidth>ncols-16)errorwidth=ncols-16;
    errorheight=textheight(errormessage,errorwidth);
    wberror = newwin(errorheight+4,errorwidth+8,(nlines-(errorheight+4))/2,(ncols-(errorwidth+8))/2);
    if(!wberror)outofmemory();
    perror = new_panel(wberror);
    wattrset(wberror,COLOR_PAIR(3));
    werase(wberror);
    wbkgd(wberror,COLOR_PAIR(3));
    box(wberror,0,0);
    windowtitle(wberror,"Error!");
    werror = innerwindow(wberror);

    wprintw(werror,errormessage);
    update_panels();
    doupdate();
    wgetch(werror);

    delwin(werror);
    del_panel(perror);
    delwin(wberror);
    update_panels();
    doupdate();
}

WINDOW * innerwindow(WINDOW * outerwindow)//creates an area within another window for purposes of displaying text/menus etc with a margin, keypad enabled
{
    WINDOW * wtemp;
    getmaxyx(outerwindow,nlines,ncols);
    wtemp = derwin(outerwindow,nlines-4,ncols-8,2,4);
    if (!wtemp) outofmemory();
    keypad(wtemp,TRUE);
    return wtemp;
}

void donothing()
{
    int pointless;
    pointless++;
    printw("Face!");
    refresh();
}

void windowtitle(WINDOW * window, char * title)//writes the given string to the given window (top centre)
{
    int textlength;
    textlength = strlen(title);
    getmaxyx(window,nlines,ncols);
    if (textlength>ncols-2)
    {
        mvwaddnstr(window,0,1,title,ncols-5);
        waddstr(window,"...");
    }
    else
    {
        mvwaddstr(window,0,(ncols-textlength)/2,title);
    }
}

int textwidth (char * text)//returns the width of a given string (which may include newlines) in chars when displayed without wrapping (for purposes of determining optimum window width)
{
    int i=0,j=0,k=0;
    while (text[i]!='\0')
    {
        if (text[i]=='\n')
        {
            k=j>k?j:k;
            j=0;
        }
        else j++;
        i++;
    }
    k=j>k?j:k;
    return k;
}

int textheight (char * text, int width)//returns the height of a given string (which may include newlines) in lines when displayed wrapped to the given width (for purposes of determining optimum window width)
{
    int i=0,j=0,k=1;
    while (text[i]!='\0')
    {
        if (text[i]=='\n')
        {
            k++;
            j=0;
        }
        else j++;
        if (j>width)
        {
            k++;
            j=1;
        }
        i++;
    }
    return k;
}

void showscore()
{
    donothing();
}

int main(int argc, char* argv[])
{
    startup();//star curses mode
    WINDOW * wbmainmenu, * wmainmenu;//main menu window for title and border, subwindow for text
    PANEL * pmainmenu;//attach to panel (panels library just makes life easier)
    MENU* mainmenu;
    ITEM ** mainmenuitems;//this pointer will be passed to the menu
    char * mainmenuchoices[][2] = //strings for menu
    {
        {"v:","View Statistics"},
        {"t:","Test Me!"},
        {"l:","Load"},
        {"m:","Manage Database"},
        {"s:","Save"},
        {"x:","Exit"}
    };
    void (*mainmenupointers[])() = /* This is an array of pointers to functions *
                                      * with no parameters and no return value... *
                                      * Or at least I think it is. *brain melts*  */
    {
        showscore,
        testme,
        reloaddatabase,
        databasemenu,
        savedatabase,
        shutdown
    };
    
    ITEM * ITEMselected; //this will point to selected item
    void (*pselected)(); //this will point to the function attached to selected item

    int i,numberofchoices = ARRAY_SIZE(mainmenuchoices);
    int welcomeflag = 0;
    int menuchoice = '\0';

    windowtitle(stdscr,"Vocab Tester Version N by Rob Davies");
    wbmainmenu = nicebigwindow();
    pmainmenu = new_panel(wbmainmenu);
    windowtitle(wbmainmenu,"Main Menu");
    wmainmenu = innerwindow(wbmainmenu);

    loaddatabase();

    if(!(mainmenuitems = (ITEM**)calloc(numberofchoices+1,sizeof(ITEM*)))) outofmemory();
    for(i=0;i < numberofchoices;i++)
    {
        mainmenuitems[i] = new_item(mainmenuchoices[i][0], mainmenuchoices[i][1]);
        set_item_userptr (mainmenuitems[i],mainmenupointers[i]);
    }
    mainmenuitems[numberofchoices] = (ITEM *)NULL;
    if (!welcomeflag) {wprintw(wmainmenu,"Welcome to the ");welcomeflag++;}
    wattron(wmainmenu,A_BOLD);
    wprintw(wmainmenu,"Vocab Test, Version N.");
    wattroff(wmainmenu,A_BOLD);
    mainmenu = new_menu(mainmenuitems);
    set_menu_win(mainmenu,wmainmenu);
    set_menu_sub(mainmenu,derwin(wmainmenu,6,19,5,4));
    set_menu_back(mainmenu,COLOR_PAIR(1));
    menu_opts_off(mainmenu,O_NONCYCLIC);
    post_menu(mainmenu);
    update_panels();
    doupdate();
    while (tolower(menuchoice)!='x')
    {
        menuchoice=wgetch(wmainmenu);
        switch (tolower(menuchoice))
        {
            case 'x': shutdown();
                      break;
            case KEY_UP: menu_driver(mainmenu,REQ_UP_ITEM);break;
            case KEY_DOWN: menu_driver(mainmenu,REQ_DOWN_ITEM);break;
            case 10:    ITEMselected = current_item(mainmenu);
                        pselected = item_userptr(ITEMselected);
                        pselected();
                        break;
            //case 'v': showscore();break;
            //case 't': testme(); break;
            case 's': savedatabase();break;
            case 'l': reloaddatabase();break;
            case 'm': databasemenu(); break;
            //default: wprintw(wmainmenu,"Invalid choice. Please try again.\n");break;
        }
        update_panels();
        doupdate();
    }
    unpost_menu(mainmenu);
    free_menu(mainmenu);
    for (i=0;i<numberofchoices;i++)
        free_item(mainmenuitems[i]);
    del_panel(pmainmenu);
    delwin(wmainmenu);
    delwin(wbmainmenu);
    shutdown();
    return 0;
}