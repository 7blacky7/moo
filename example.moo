# ===========================
# moo Showcase — All Features
# ===========================

# --- Variables & Constants ---
set name to "World"
set age to 25
set pi to 3.14159
const MAX to 100

# --- Output ---
show "Hello " + name

# --- Math ---
set result to (10 + 5) * 2
set remainder to 17 % 5
set power to 2 ** 10
show result
show remainder
show power

# --- Conditions ---
if age >= 18:
    show "Adult"
else if age >= 13:
    show "Teenager"
else:
    show "Child"

# --- Loops ---
set i to 0
while i < 5:
    if i == 3:
        i += 1
        continue
    show i
    i += 1

# --- Lists ---
set colors to ["red", "green", "blue"]
for color in colors:
    show color

show colors[0]
colors[1] = "yellow"
show colors

# --- Dictionaries ---
set person to {"name": "Anna", "age": 30}
show person["name"]

# --- Functions ---
func greet(who, greeting = "Hello"):
    return greeting + ", " + who + "!"

show greet("Max")
show greet("Lisa", "Howdy")

# --- Lambdas ---
set double to (x) => x * 2
show double(21)

# --- Classes ---
class Animal:
    func create(name, sound):
        this.name = name
        this.sound = sound

    func speak():
        return this.name + " says " + this.sound

set dog to new Animal("Rex", "Woof")
show dog.speak()
show dog.name

# --- Inheritance ---
class Dog(Animal):
    func create(name):
        this.name = name
        this.sound = "Woof"

    func fetch():
        return this.name + " fetches!"

set bello to new Dog("Bello")
show bello.speak()
show bello.fetch()

# --- Error handling ---
try:
    set x to 10 / 0
catch error:
    show "Error caught!"

# --- Match/Switch ---
set day to "Monday"
match day:
    case "Monday":
        show "Start of the week!"
    case "Friday":
        show "Almost weekend!"
    default:
        show "A normal day"

# --- None (null) ---
set empty to none
if empty == none:
    show "Variable is empty"
