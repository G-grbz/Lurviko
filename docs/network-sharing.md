# Samba folder sharing

A single local item's **Properties** includes **Network sharing**. For a folder
you own, enter a share name and optional comment, then choose **Share folder**.
Read-only access is the default. Clear it to permit writes, within the existing
filesystem permissions. The panel never changes file ownership or permissions.
The share's `smb://host/name` address can be selected and copied.
The panel explains which username/password to use and includes a selectable
`sudo smbpasswd -a "$USER"` setup command. It does not choose a password for you.
Properties text fields use Lurviko's themed editing menu and preserve selections.

At `smb://`, **Shares on this computer** lists the existing local Samba usershares
independently of network host discovery. Opening one uses `smb://localhost/name/`
on this computer; use the address in Properties when connecting from another
computer. Local definitions are read only when this view opens or you refresh it.
There is no periodic network query or passwordless fallback.

Files are shared through their containing folders; a file's Properties explains
this instead of silently exposing the whole parent directory. Remote items,
multiple selections and symbolic links cannot create a share through this panel.

Existing shares can be updated or removed. If a folder has multiple share names,
choose the name first; removing one does not remove the others. A name already
used by a different folder cannot be overwritten. Renaming requires removing the
old definition explicitly. Custom ACLs and guest-enabled definitions are shown
without replacing their access rules with the panel's simpler read/write choices.
Removing a definition does not delete its folder or contents.

The feature uses Samba's existing `net usershare` commands asynchronously, with
argument lists and an option terminator, never a shell. There is no background
polling or additional directory-size work. The Properties modal uses Lurviko's
normal theme surface, including the native Qt/KWin appearance when enabled.

## Server setup

Samba must be installed and its service running. An administrator must configure
usershares in `/etc/samba/smb.conf`, for example:

```ini
[global]
    workgroup = WORKGROUP
    server role = standalone server
    security = user
    map to guest = Never
    usershare path = /var/lib/samba/usershares
    usershare max shares = 100
    usershare owner only = yes
    usershare allow guests = no
```

The usershare directory must be owned by root and writable by the sharing user's
group, with mode `1770`. Choose a dedicated sharing group on multiuser machines;
group membership changes may require signing in again. This setup does not export
any folder until a user creates a share. See Samba's official
[usershare directory requirements](https://www.samba.org/samba/docs/current/man-html/smb.conf.5.html#USERSHAREPATH)
and [usershare commands](https://www.samba.org/samba/docs/current/man-html/net.8.html).

Authenticated network access also needs a Samba account/password. For a local
account that has not been added, its owner can ask an administrator to run:

```sh
sudo smbpasswd -a "$USER"
```

Enter that password locally in the terminal, not in a chat. The Properties panel
does not create accounts, store passwords, enable guests or open firewall ports.
Actual reachability from another machine still depends on the firewall, network
and the client's credentials.

## Validation

The repair on the development machine recreated a missing `smb.conf`, passed
`testparm`, and started the already enabled `smb.service`. Its root-owned usershare
directory grants access to the existing primary user group, without requiring a
logout or adding group memberships. Guest usershares are disabled. TCP 445 and a
real temporary empty-folder create/info/delete cycle passed. No user directory
was automatically exported. Password-authenticated access from another computer
has not been tested.

The isolated tests cover create/update/delete, Samba's trailing-comma ACL output,
literal command arguments, option-like comments, name collisions, preservation
of custom ACLs, files/symlinks/remote URLs, errors and obsolete inspection results.
Run `python3 tests/run-samba-share-tests.py`. The properties UI is also checked by
the existing native Properties and QML tests.
