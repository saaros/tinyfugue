;;;; Socket line counts on status line

/loaded activity_status.tf

/require -q world-q.tf
/require -q textencode.tf

/status_edit @world:11
/status_rm @read
/status_rm insert
/status_rm @log
/status_rm @active
/set status_int_read=nread() ? "R" : ""
/set status_var_insert=insert ? "" : "O"
/set status_int_log=nlog() ? "L" : ""
/status_add -x -A"@world" activity_status
/status_add -A"activity_status" @read:1:r insert:1:r @log:1

/def -qi -Fp2147483647 -mglob -h'WORLD' update_activity_fg = \
    /let label=$[textencode(fg_world())]%; \
    /set activity_color_%{label}=%; \
    /repeat -0 1 /update_activity

/def -i activity_color = \
    /if (${world_name} !~ fg_world()) \
	/let var=activity_color_$[textencode(${world_name})]%; \
	/eval \
	    /if (!regmatch("(^|,)%1(,|$$)", %var)) \
		/set %var=%%{%var},%1%%; \
	    /endif%; \
    /endif

/def -E'${world_name} !~ fg_world()' -qi -Fp2147483647 -mglob -t'*' \
    update_activity_trig = \
	/update_activity_delayed

/def -E'${world_name} !~ fg_world() & moresize("")' \
  -qi -Fp2147483647 -mglob -h'DISCONNECT' \
    update_activity_disconnect_hook = \
	/activity_queue_hook ${world_name}%; \
	/update_activity

;; Abbreviate a string, for the status bar.  User can define custom
;; abbreviations with "/set_status_abbr <string> <abbr>", or writing
;; his own /status_abbr_hook.  If both of those fail, pick out the initials
;; from the string.
/def -i status_abbr = \
    /if /ismacro status_abbr_hook%; /then \
	/let abbr=$[status_abbr_hook({*})]%; \
	/if (abbr !~ "") /result abbr%; /endif%; \
    /endif%; \
    /let abbr=%; \
    /test abbr:=status_abbr_$[textencode({*})]%; \
    /if (abbr !~ "") \
	/result abbr%; \
    /elseif (strlen({*}) <= 2) \
        /result {*}%; \
    /elseif (regmatch("^\\(unnamed(\\d+)\\)$$", {*})) \
        /result strcat('u', {P1})%; \
    /endif%; \
    /let n=$[status_width('activity_status') / $(/length %active_worlds) - 4]%;\
;   Abbreviate the name only as much as necessary to fit in n characters.
;   Try to keep capitals, beginnings of words, and digits; discard everything
;   else as needed.
    /let name=%*%; \
    /while (strlen(name) > n & \
        regmatch("((?:[A-Z]|(?<![a-z])[a-z])[a-z]*)[a-z]((?:[^a-z]*(?:(?<![a-z])[a-z])?)+)$", name)) \
        /let name=%PL%P1%P2%; \
    /done%; \
    /while (strlen(name) > n & \
        regmatch("[^A-Za-z0-9]", name)) \
        /let name=%PL%PR%; \
    /done%; \
    /result name

;; /set_status_abbr <world> <abbr>
;; <abbr> may contain @{} attributes
/def set_status_abbr = /set status_abbr_$[textencode({1})]=%-1
/def unset_status_abbr = /unset status_abbr_$[textencode({1})]

;; Activity message is confusing with 5.0's per-world virtual screens, and
;; activity_status tells you what worlds have activity.
/def -i -ag -hactivity gag_activity

;; NB: %* is not current world
/def -i update_activity_world = \
    /let n=$[moresize("", {*})]%; \
    /test activity_color_$[textencode({*})]%; \
    /echo -p - \
	@{%?}$[is_open({*})?"":"!"]$[status_abbr({*})]:\
	$[n < 1000 ? n : strcat(n/1000, "k")]@{n}

/def -i update_activity = \
    /if (update_activity_pid) \
	/kill %update_activity_pid%; \
	/set update_activity_pid=0%; \
    /endif%; \
    /set activity_status=$(/mapcar /update_activity_world %active_worlds)

/def update_activity_delayed = \
    /if (update_activity_pid) \
	/kill %update_activity_pid%; \
    /endif%; \
    /if (moresize("") == 1 | mod(moresize(""), 50) == 0) \
	/repeat -0 1 /update_activity%; \
	/set update_activity_pid=0%; \
    /else \
	/repeat -1 1 /update_activity%; \
	/set update_activity_pid=%?%; \
    /endif

