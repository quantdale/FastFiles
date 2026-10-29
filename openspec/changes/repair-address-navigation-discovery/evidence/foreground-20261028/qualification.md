# Foreground retry regression

Fresh independent navigation source review found no P1/P2 and suggested a missing negative mock: address edit focus true while owned foreground is false. Worker prepared the isolated test edit but exhausted workspace credits before validation. Root inspected and completed it without retrying the provider.

Base freshly fetched PR31 `30a2bc60bc802648259ca2dad819b54c9c420211`, exact 0/0 relationship. Test-only change exposes the closed foreground mock and restores it in finally. Production helper is unchanged.

Real production function AST in closed mocks:14/14 assertions PASS. Isolated ignored build copy removes only the foreground conjunction: exactly the new foreground assertion FAILS, exit1. This proves the assertion distinguishes the missing guard without touching production source or sending native input. Positive source-focus recommit and lost-edit-focus cases remain passing. No app, GUI input, user data, privilege or termination experiment. This is regression coverage, not new runtime qualification or proof of the historical navigation failure's cause.
