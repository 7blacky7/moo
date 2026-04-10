# English test

set name to "World"
set age to 25
show "Hello " + name
show age

if age >= 18:
    show "Adult"
else:
    show "Child"

set colors to ["red", "green", "blue"]
for color in colors:
    show color

set person to {"name": "Anna", "age": 30}
show person["name"]

func greet(who):
    return "Welcome, " + who + "!"

show greet("Max")

set i to 0
while i < 3:
    show i
    i += 1
