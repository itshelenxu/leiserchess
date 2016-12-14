hash_file = open("new_hash.txt", "r")
move_file = open("new_moves.txt", "r")
score_file = open("new_scores.txt", "r")

hashline = hash_file.readline()
moveline = move_file.readline()
scoreline = score_file.readline()

hcount = len(hashline.split(","))
mcount = len(moveline.split(","))
scount = len(scoreline.split(","))

print hcount, mcount, scount
