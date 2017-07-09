#!/usr/bin/perl

# Written by Rory McInerney, rorymcinerney@gmail.com
# feel free to use this for whatever, I make it public domain
use strict;
use warnings;
use Net::LDAP;
use File::Find;

# LDAP user name
my $uid = "cn=piler,cn=users,dc=yourdomain";
# LDAP user password
my $bindPass = "youpass";
# LDAP password 
my $ldapServer = "ldap://yourdc.yourdomain";
# dummy not found email address to use
my $notfound = "noexist\@domain.com";
# ldap base
my $base = "ou=Users,dc=domain";
# define some dirs we're going to use
my $inputdir = '/var/pst-import/in';
my $workdir = '/var/pst-import/working';


# the script runs as piler user and needs the correct permissions
sub takeownership {
	system("sudo chown piler $workdir");
	system("sudo chown piler $inputdir");
	system("sudo chmod 755 /var/pst-import");
}

# quick function to trim whitespace
sub  trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };


# this rewrites To: and Cc: headers
sub rewrite_to_headers {
	#initiate variables
	my @files;
	my $start_dir = "$_[0]";  # top level dir to search
	
	# find all the files and return the variable
	find( sub { push @files, $File::Find::name unless -d; }, $start_dir );
	
	# This iterates through all the email files found
	foreach my $mailpath (@files) {
	
		# REWRITE To: Headers
		# this matches to see if it's a sent item, based on the file structure of the PST
		if($mailpath =~ /Sent Items/i) { # if it's a sent item
			# this extracts the line in the header to manipulate 
			my $line =  `grep \"To:\" \"$mailpath\" -m 1`;
			# lose the newline
			chomp($line);
			my $origline = $line;
			# lose the To: bit off the start of the header
			$line =~ s/To: //g;
			# split the mush of addresses into an array of parts on the semicolons
			my @adds = split(/\;/, $line);
			my $newline = "To:";
			# cycle through all the address fragments
			foreach my $add (@adds) {
				$add = trim($add);
				# if it matches the format of an email address
				if($add =~ /\'.+\@.+\'/) {
					$newline = $newline . " ".$add.";";
				} else {
					my $email = find_ldap_mail($add);
					$email =~ s/\@/\\\@/gi;
					$newline = $newline . " ".$add." <$email>;";
				}
			}
			chop($newline);
			my $command = "perl -i -p -e \"s/$origline/$newline/g;\" \"$mailpath\"\n";
			`$command`;
			
			
			####  REWRITES CC: HEADERS
			my $result = `head -n 5 \"$mailpath\" | grep "Cc:" -i -m 1`;
			if($result) {
				my $line =  `grep \"Cc:\" \"$mailpath\" -m 1`;
				# lose the newline
				chomp($line);
				my $origline = $line;
				# lose the To: bit off the start of the header
				$line =~ s/Cc: //g;
				# split the mush of addresses into an array of parts on the semicolons
				my @adds = split(/\;/, $line);
				my $newline = "Cc:";
				# cycle through all the address fragments
				foreach my $add (@adds) {
					$add = trim($add);
					# if it matches the format of an email address
					if($add =~ /\'.+\@.+\'/) {
						$newline = $newline . " ".$add.";";
					} else {
						my $email = find_ldap_mail($add);
						$email =~ s/\@/\\\@/gi;
						$newline = $newline . " ".$add." <$email>;";
					}
				}
				chop($newline);
				my $command = "perl -i -p -e \"s/$origline/$newline/g;\" \"$mailpath\"\n";	
				`$command`;
			}
		}	
	}
}

sub rewrite_send_headers {
	# argument is the folder containing the unzipped (from pst) email files
	# get a list of the files in the directory (see subs)
	my @mails = getfiles(@_);
	# get the displayname from the emails (see subs)
	my $dN = get_displayname(@mails);
	# query active directory to get email to write from the dN
	my $result = find_ldap_mail($dN);
	# escape the special @ symbol to make system call work properly
	$result =~ s/\@/\\\@/;


	# for each email in the list of emails...
	foreach my $mailpath (@mails) { 
		if($mailpath =~ /Sent Items/i) {  # if it's a sent item
			system("perl -i -p -e \'s/MAILER-DAEMON/$result/g;\' \"$mailpath\"\n"); # rewrite the mailer daemon with the correct stuff
		}
	}
}

# this sub gets the DN from the first email in the sent items folder in the pst we are working with
sub get_displayname {
	my @fragments; # initialise this
	foreach my $mailpath (@_) { # the argument is all the files in the pst broken open
		if($mailpath =~ /Sent Items/i) { # if it's a sent item
			my $line =  `grep \"From:\" \"$mailpath\" -m 1`; # find the display name of the first email sent
			@fragments = split(/\"/, $line); # isolate it
			last;	# once we've found it, we've found it
		}
	}
	return $fragments[1]; #return it
}

# sub for getting the filenames, takes directory as argument
sub getfiles {
	#initiate variables
	my @files;
	my $start_dir = "$_[0]";  # top level dir to search
	# find all the files and return the variable
	find( sub { push @files, $File::Find::name unless -d; }, $start_dir );
	return @files;
}

#sub for finding the ldap mail attribute from a displayName
sub find_ldap_mail {
		# escape ( and )
		my $filter = $_[0];
		$filter =~ s/\(/\\\(/gi;
		$filter =~ s/\)/\\\)/gi;
				
        # variables we using
        my $uid = "cn=piler,cn=users,dc=multigroupplc";
        my $bindPass = "M1ssion1977";
        my $ldapServer = "ldap://svcadc03.multigroupplc";
        # connect to ldap server
        my $ldap = Net::LDAP -> new ($ldapServer) || die "Could not connect to server\n";

        # bind to ldap server
        my $result = $ldap -> bind($uid, password => $bindPass);

        #we're looking for the mail attribute so tell it that
        my $attrs = [ 'cn','mail' ];

        # ask the ldap server
        $result =       $ldap->search(  base => "ou=Servoca Plc,dc=multigroupplc",
                                        filter => "(&(objectClass=Person)(displayName=$filter))",
                                        attrs => $attrs
        );

        # not really sure what this does but it makes it work
        $result->code && die $result->error;

        # initialise this as you're not allowed inside the foreach loop
        my @foundemails;

        # look through the entries and put each result into an array
        foreach my $entry ($result->all_entries) {
                push @foundemails,$entry->get_value('mail');
        }
		if(@foundemails) {
			#return what we've found as a scalar value
			return $foundemails[0];
		} else { 
			return "noexist\@servoca.com";
		}
        #unbind from the AD server
        $result = $ldap->unbind;
}


# this processes it
sub processpst {
	my @a = split(/\./, $_[0]);												# chop off the first name of the pst
	my $newname = lc $_[0];													# rename it in  lowercase
	system("mv $inputdir/$_[0] $workdir/$newname");							# move it to the working directory
	system("mkdir $workdir/$a[0]");											# make output directory for readpst
	system("readpst -D -M -b -q -o $workdir/$a[0] $workdir/$newname\n");	# crack the pst open into the output folder
	rewrite_send_headers("$workdir/$a[0]");									# rewrite the from: header from mailer-daemon
	rewrite_to_headers("$workdir/$a[0]");									# rewrite the to/cc headers for piler to read
	system("pilerimport -D -d $workdir/$a[0]  -r");							# import emails into piler archive
	system("rm $workdir/$newname");											# tidying up
	system("rm -rf $workdir/$a[0]");										# more tidying up
}	

# SCRIPT OPERATES FROM HERE

# get the right permissions
takeownership();

# do we have any files?
opendir DIR, $inputdir or die;
my @rawfiles= readdir DIR;
closedir DIR;

# more importantly, do we have any psts?
foreach my $file (@rawfiles) {
	if($file =~ /.*\.pst$/i) {
	processpst($file);
	}
}

