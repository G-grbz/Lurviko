# Connect Google Drive and OneDrive

Lurviko supports browsing and transferring files through your Google Drive and
OneDrive accounts. It does not include a shared OAuth client: create your own
client once, enter its details in Lurviko, then sign in using your browser.
Your Google or Microsoft password is entered on the provider's website, never
in Lurviko's settings.

## Before you start

- Have a Google account or a Microsoft account with OneDrive available.
- Use a working default browser on the same computer as Lurviko. The browser
  returns authorization to a temporary local callback listener.
- Enable and unlock KWallet if you want Lurviko to remember the connection.
- Create the matching client using the [Google Drive guide](google-drive-setup.md)
  or the [OneDrive guide](onedrive-setup.md).

## Connect in Lurviko

1. Open **Discover** from the sidebar, then find **Cloud Integrations**.
2. If the provider is hidden, select **Manage clouds** and enable it.
3. Click the gear on its card to open **OAuth settings**. An unconfigured card
   also opens these settings when clicked.
4. Enter the fields from the provider guide and select **Save**.
5. Click the provider card again to start browser authorization. Saving the
   settings alone does not sign you in.
6. Choose your account, review the permissions and approve access. Keep Lurviko
   open until the browser shows the authorization-completed message.
7. Return to Lurviko. The card should show **Connected**; click it to browse.

Each provider currently has one configured client and one account session.
These integrations browse remote files; connecting does not automatically
mirror your Drive onto the local filesystem.

## Upload files

After connecting, select local files and open the context menu's **Share**
submenu. Choose **Send to Google Drive** or **Send to OneDrive**. These actions
upload to the account's root folder. The actions appear only when the provider
is enabled and connected, and the selection is eligible for upload.

## Remembering and removing access

Lurviko stores refresh tokens and the optional Google client secret through
KWallet. Client IDs and the Microsoft tenant choice are saved in application
settings. If KWallet is unavailable or its unlock request is declined, session
credentials cannot be persisted there and you may need to authorize again after
restarting. No plaintext token fallback is used.

For OneDrive, **Reset** in its OAuth settings clears the configured client,
tenant choice and locally stored session. This does not revoke authorization
at Microsoft. Hiding an integration in **Manage clouds** also does not revoke
provider access. To remove an app's authorization, use your provider's account
security/app-permission settings; organization policies may require an admin.

## Connection troubleshooting

| Symptom | What to check |
| --- | --- |
| The card still says authorization is pending after saving | Click the card to start browser sign-in. |
| Connection error | Click the card for details, then use **OAuth settings** to correct the client configuration. See the provider guide for error-specific steps. |
| Waiting for browser authorization | Complete sign-in in the browser on this computer. Do not open the callback on another device or close Lurviko before it returns. |
| The local callback cannot start | Check whether the browser/network environment permits local loopback connections. Lurviko chooses a temporary port; no public server or inbound internet port is needed. |
| Authorization required again | Click the card to sign in again. A saved refresh token can expire or be revoked. |
| It connects, but forgets the account after restart | Confirm that KWallet is running, unlocked and permits Lurviko to store credentials. |

When reporting an error, include its error code and provider name. Remove tokens,
client secrets and authorization callback URLs from screenshots or logs.

## Optional environment defaults

The UI is the simplest setup method. These environment variables provide
defaults when the corresponding saved settings are absent:

| Variable | Value |
| --- | --- |
| `LURVIKO_GOOGLE_CLIENT_ID` | Google Desktop app client ID |
| `LURVIKO_GOOGLE_CLIENT_SECRET` | Optional Google desktop client secret |
| `LURVIKO_ONEDRIVE_CLIENT_ID` | Microsoft Application (client) ID |
| `LURVIKO_ONEDRIVE_TENANT` | `common` by default; see the OneDrive guide |

Saved UI values take precedence over environment defaults. Do not commit client
secrets, downloaded credential JSON or account tokens to your repository.
