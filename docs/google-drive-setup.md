# Google Drive: your own OAuth client

Lurviko does not ship shared Google OAuth credentials.

1. Create/select a project in [Google Cloud Console](https://console.cloud.google.com/).
2. Enable **Google Drive API**.
3. Configure the OAuth consent screen/audience. For an external application
   in testing mode, add the Google accounts you will use as test users.
4. Create an **OAuth client ID** of type **Desktop app**.
5. Enter its client ID/secret in Lurviko's Google Drive settings and connect.
   Environment alternatives are `LURVIKO_GOOGLE_CLIENT_ID` and
   `LURVIKO_GOOGLE_CLIENT_SECRET`. Never commit credentials or tokens.
6. Review Google's permission screen. Lurviko uses a desktop loopback callback
   and stores account credentials through KWallet.

## Scope and publishing

`https://www.googleapis.com/auth/drive` is a **restricted** scope. Lurviko lists
and manages existing Drive files. Silently replacing it with `drive.file`
would limit access to files created by, opened with or shared with the app
and change browsing behavior.

Your own OAuth client does not automatically exempt an application from
Google's policies. For a shared/published client, follow Google's consent,
verification and applicable security-assessment requirements. Testing-mode
credentials have Google's testing limitations and are not production approval.

References: [Drive scopes](https://developers.google.com/workspace/drive/api/guides/api-specific-auth),
[desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app),
[restricted-scope verification](https://developers.google.com/identity/protocols/oauth2/production-readiness/restricted-scope-verification).
