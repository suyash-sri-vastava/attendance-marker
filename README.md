# Attendance Planner (FOCP assignment)

Tells a student, subject by subject, how many classes they can still skip
(or must attend) to stay above the required attendance percentage.

Written in plain C. The program is its own small web server.

## Run on your own computer
    gcc -O2 -Wall -Wextra -o attendance server.c
    ./attendance
Open http://localhost:8080

## Edit team name and scholar numbers
Top of `server.c`, between the "EDIT THIS PART" comments.

## Deploy free on Render
1. Put `server.c`, `Dockerfile`, `README.md` in a new GitHub repository.
2. render.com -> New -> Web Service -> pick the repository.
3. Language/Runtime: Docker. Instance type: Free. Create.
4. Share the `https://<name>.onrender.com` link when it says "Live".
