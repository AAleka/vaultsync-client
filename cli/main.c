#include <stdio.h>
#include <string.h>
#include <vaultsync/vaultsync.h>

static void print_help(void);

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        {
            print_help();
            return 0;
        }
        else if(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)
        {
            printf("vaultsync version %s\n", vs_version());
            return 0;
        }
    }

    return 0;
}

static void print_help(void)
{
    const char* help_msg = \
        "This is a backup application.\n"
        "Usage: vaultsync_cli <args>\n"
        "<args>:\n" \
        "    --help    / -h - prints this help message\n"
        "    --version / -v - prints the application version\n";
    
        printf("%s\n", help_msg);
}