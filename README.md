Just another pixiv leeching/management tool. Compilation requires a C compiler (preferably GCC), libcurl, and cJSON.

## Usage

First, you need to log into pixiv, and then dump your cookies in Netscape textfile format. You can probably find an extension for your browser that does this. I suggest only including the cookies associated with pixiv.net (although it's not totally necessary, irrelevant cookies are filtered out). If the tool ever stops working, it's likely because your cookies went out of date and you need to re-export them. If it still doesn't work, then the program itself is probably out of date.

After you've got it compiled, use it via the command line. There are three kinds of parameters, "options", "commands", and "arguments". The "command" specifies what you want to do, and you must specify exactly one of them. "Options" change how certain commands behave. The meaning of "arguments" change depending on which command you specify.

### Commands

* `--get-illust` downloads illusts.
* `--get-user-illusts` retrieves all of a user's illusts IDs.
* `--bookmark` adds illusts to your bookmarks.
* `--delete-bookmark` removes illusts from your bookmarks. Please use this sparingly, since the program has to make two requests to the server, one to get the bookmark ID for the illust and another to remove it from your bookmarks.
* `--delete-bookmark-id` removes bookmark IDs from your bookmarks. This is really only useful in combination with `--get-bookmark-id(s)`.
* `--get-bookmark-id` gets the bookmark IDs corresponding to illusts. They must actually be IN your bookmarks for this command to work.
* `--get-bookmarks` gets all the illust IDs in a user's bookmarks.
* `--get-bookmark-ids` gets all the bookmark IDs in a user's bookmarks. The user ID must be yours, or the command won't work.
* `--follow` follows users.
* `--unfollow` unfollows users.
* `--get-following` gets all the user IDs being followed by a particular user.

### Options

* `-?` or `--help` shows the help display.
* `-i` sets an input file name. The file should be a plaintext file with one argument per line. You can specify multiple input files, and any additional arguments you specify on the command line are also interpreted.
* `-o` sets the output file name, for any command which writes a list of IDs to disk. The written file can also be used as an input to another command with `-i`.
* `-P` sets the output path name, for any command which batch-downloads files. By default, this is the current directory.
* `--cookies` sets the file name of the cookies you exported.
* `--offset`, for any command which gets a list of things, specifies where to start at.
* `--max`, for any command which gets a list of things, specifies the maximum amount of things to fetch.

### Usage examples

Download illust ID 117875739:

`pixiv --cookies cookies.txt --get-illust 117875739`

Get a list of user ID 11211325's works:

`pixiv --cookies cookies.txt -o works.txt --get-user-illusts 11211325`

Download the above works to a folder called `works`:

`pixiv --cookies cookies.txt -P works -i works.txt --get-illust`

Get 12 of user ID 5057400's bookmarks, starting at index 6:

`pixiv --cookies cookies.txt -o bookmarks.txt --offset 6 --max 12 --get-bookmarks 5057400`

Delete your first 12 bookmarks (assuming your user ID is 5000000):

```
pixiv --cookies cookies.txt -o bookmarks.txt --max 12 --get-bookmark-ids 5000000
pixiv --cookies cookies.txt -i bookmarks.txt --delete-bookmark-id
```

### Limitations

The below things may be rectified in the future, but for now, they're not.

* This tool is not capable of filtering illusts by tag, filtering out AI-generated, etc.
* This tool cannot convert ugoira to a real animated format, it only downloads the raw .zip of images.
* This tool cannot tag new bookmarks/add them privately.