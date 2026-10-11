# Google Drive: your own OAuth client

Lurviko does not ship shared Google OAuth credentials. This guide sets up your
own client; see [Connect Google Drive and OneDrive](cloud-accounts.md) for the
connection steps in Lurviko.

## Create the Google client

1. Create or select a project in [Google Cloud Console](https://console.cloud.google.com/).
2. Under **APIs & Services > Library**, find **Google Drive API** and enable it.
3. Open **Google Auth platform**. Complete **Branding** with an application name
   such as `Lurviko Personal` and the required contact details.
4. Under **Audience**, choose the audience appropriate to your account. For a
   personal Google account, use **External**. While the application is in
   **Testing**, add the Google account you will sign in with under **Test users**.
5. Under **Data Access**, add `https://www.googleapis.com/auth/drive`.
   Review the [consent-screen setup guide](https://developers.google.com/workspace/guides/configure-oauth-consent)
   for audience and scope configuration.
6. Under **Clients > Create Client**, select **Desktop app**, give it a name
   and create it. Copy the **Client ID**. If you download its JSON, use the
   `client_id` and optional `client_secret` fields inside `installed`.
   See Google's [desktop-client creation steps](https://developers.google.com/workspace/guides/create-credentials#desktop-app).

Console labels can change; the essential choice is a **Desktop app** OAuth
client rather than an API key, service account or Web application client.

## Enter the details in Lurviko

Open **Discover > Cloud Integrations**, then the gear on **Google Drive**.

| Lurviko field | Value |
| --- | --- |
| Application (client) ID | The Google Desktop app's Client ID |
| Client Secret (optional) | Its `client_secret`, if supplied in the downloaded JSON |

Select **Save**, then click the Google Drive card to authorize in your browser.
Lurviko uses PKCE and an `http://127.0.0.1:<temporary-port>/` loopback callback.
A Google Desktop app supports this flow without a public callback server.
The desktop client secret is optional for token exchange; if Google reports
`invalid_client` or a missing secret, verify the client type and copy the values
from its Desktop app JSON. See [Google's desktop OAuth protocol](https://developers.google.com/identity/protocols/oauth2/native-app).

## Common errors

| Error or symptom | Action |
| --- | --- |
| `403 access_denied` while testing | Add the exact account used in the browser to **Audience > Test users**. Also check any Workspace administrator restrictions. |
| `invalid_client` / missing `client_secret` | Confirm the client is **Desktop app** and both fields come from the same client. Enter the optional secret if Google requires it for that client. |
| `redirect_uri_mismatch` | Recheck the client type. Lurviko sends a loopback IP callback with a temporary port; a Web application registration is not the intended setup. |
| Drive API disabled / access not configured | Enable **Google Drive API** in the project that owns the client. |
| Sign-in is requested again after about a week | External apps in **Testing** using the Drive scope receive refresh tokens that expire after seven days. Reauthorize from the card; changing publishing status requires reviewing Google's policies. See [refresh-token expiration](https://developers.google.com/identity/protocols/oauth2#expiration). |

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
