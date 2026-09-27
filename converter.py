from music21 import converter, note

def main(title,BPM,file):
    notes = {
        'C#5': -7,
        'G#4': -6,
        'E4': -5,
        'B4': -4,
        'C5': 7,
        'G4': 6,
        'D#4': 5,
        'A#4': 4,
        'B4': -3,
        'F#4': -2,
        'D4': -1,
        'A4': '-0',
        'A#4': 3,
        'B-4': 3,
        'F4': 2,
        'C#4': 1,
        'G#4': 0,
        'A-4': 0,
        'A4': 33,
        'E4': 22,
        'C4': 11,
        'G4': '00',
        }
    
    command = f'{title},{int(60000/BPM/8)}'
    score = converter.parse(file)

    for element in score.flatten().notesAndRests:
        duration = element.duration.quarterLength

        if isinstance(element, note.Note):
            tone = element.pitch.name
            pitch = element.pitch.octave
            n = tone + str(pitch)
            command = (f'{command},{notes[n]}*{int(duration*8)}')

        elif isinstance(element, note.Rest):
            command = (f'{command},{int(duration*8)}')
    print(command)

main('Speed', 100, 'Speed.mxl')