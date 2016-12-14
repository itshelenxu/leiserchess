hash_file = open('./hash.txt', 'r')
move_file = open('./moves.txt', 'r')
score_file = open('./scores.txt', 'r')

hash_line = ''
move_line = ''
score_line = ''

hashes = set()

hash_line = hash_file.readline()
move_line = move_file.readline()
score_line = score_file.readline()

new_hash_file = open('./new_hash.txt', 'w')
new_move_file = open('./new_moves.txt', 'w')
new_score_file = open('./new_scores.txt', 'w')

new_hash = ''
new_move = ''
new_score = ''

while hash_line != '':
    if hash_line not in hashes:
        hashes.add(hash_line)
        new_hash += hash_line[:-1] + ', '
        new_move += move_line[:-1] + ', '
        new_score += score_line[:-1] + ', '
    
    hash_line = hash_file.readline()
    move_line = move_file.readline()
    score_line = score_file.readline()

new_hash_file.write(new_hash)
new_move_file.write(new_move)
new_score_file.write(new_score)
