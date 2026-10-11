# OneDrive: your own Microsoft OAuth client

Lurviko uses a Microsoft public desktop client with browser sign-in and PKCE.
It does not ship a shared Microsoft client ID. See [Connect Google Drive and
OneDrive](cloud-accounts.md) for the connection steps in Lurviko.

## Register the application

1. Open the [Microsoft Entra admin center](https://entra.microsoft.com/) and
   select the directory where you can register applications. You need an Entra
   tenant and permission to create app registrations; having a personal OneDrive
   account alone does not guarantee access to that registration portal.
2. Go to **Entra ID > App registrations > New registration**.
3. Enter a name such as `Lurviko Personal`.
4. Select the supported account types. For both work/school and personal
   accounts, choose **Any Entra ID Tenant + Personal Microsoft accounts**
   (some portal versions call this **Accounts in any organizational directory
   and personal Microsoft accounts**). For a personal-only client, choose
   **Personal accounts only**.
5. Register and copy the **Application (client) ID** from **Overview**.
   This is the identifier Lurviko needs, not the Object ID or Directory ID.

Microsoft documents the prerequisites and audience choices in its
[application-registration guide](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app).

## Configure the callback and permissions

1. Open **Authentication > Add a platform > Mobile and desktop applications**.
2. Add `http://localhost` as the custom redirect URI and save it. Register it
   for the desktop platform, rather than Web or Single-page application.
   Lurviko listens on a temporary local port; Microsoft's localhost matching
   ignores that port. See [redirect URI rules](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url#localhost-exceptions).
3. Under **API permissions > Add a permission > Microsoft Graph > Delegated
   permissions**, configure `Files.ReadWrite`, `User.Read`, `offline_access`
   and `openid`, which match Lurviko's requested scopes. Use delegated
   permissions for the signed-in account. This flow needs no client secret.

`Files.ReadWrite` permits reading and modifying the signed-in user's files;
`User.Read` permits reading their profile. `openid` and `offline_access` support
sign-in and session renewal. Your organization may restrict user consent and
require administrator approval. See the [Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference#filesreadwrite).

## Enter the details in Lurviko

Open **Discover > Cloud Integrations**, then the gear on **OneDrive**.

| Lurviko field | Value |
| --- | --- |
| Application (client) ID | The application's Client ID copied from Overview |
| Tenant | Usually `common`; choose an appropriate value below |
| Registered redirect URI | Informational: `http://localhost` |

Select **Save**, then click the OneDrive card to authorize in your browser.

| Tenant value | Accounts allowed by this authority |
| --- | --- |
| `common` | Personal and work/school accounts; Lurviko's default |
| `consumers` | Personal Microsoft accounts |
| `organizations` | Work/school accounts |
| Directory (tenant) ID | Accounts permitted by that particular tenant |

The tenant value selects the authority; it does not replace the Client ID or
expand the registration's supported account types. Choose matching settings in
both places. See [Microsoft's authority choices](https://learn.microsoft.com/en-us/entra/identity-platform/v2-protocols-oidc#find-your-apps-openid-configuration-document-uri).

## Common errors

| Error or symptom | Action |
| --- | --- |
| `AADSTS50011` / redirect mismatch | Register exactly `http://localhost` under **Mobile and desktop applications**, then save the change. |
| `invalid_client` / client secret required | Verify the Application (client) ID and desktop/public-client platform. Lurviko does not send a Microsoft client secret. |
| Personal account is not accepted | The registration must support personal accounts; use `consumers` or a compatible `common` registration. |
| Work/school sign-in is rejected | Check the supported account types, tenant value and your organization's policies. |
| Administrator approval required | Ask your organization's administrator to review consent. Lurviko cannot bypass tenant policy. |
| Connected but no Drive available | Confirm OneDrive is provisioned for that account and that its required organizational license/access is available. |

To replace an incorrect local configuration, select **Reset** in Lurviko's
OneDrive OAuth settings, then enter the correct Client ID and tenant. Changing
either field clears the old local session and requires authorization again.
