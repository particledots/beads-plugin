// Which MIDI notes are held, with last-note priority. JUCE-free so it can be tested headless.
//
//  * held()    - true while at least one note is down
//  * current() - the note that decides the pitch: the most recent one still held; when every key is up it
//                stays on the last note played (so a release tail keeps its pitch). -1 until a note is played.
//  * The stack holds up to 16 notes; a 17th pushes out the oldest. Repeated note-ons for a held note
//    just move it to the top.
#pragma once
#include <array>

class NoteTracker
{
public:
    void noteOn (int note)
    {
        if (note < 0 || note > 127) return;
        remove (note);
        if (count == (int) stack.size())
        {
            for (int i = 1; i < count; ++i) stack[(size_t) i - 1] = stack[(size_t) i];
            --count;
        }
        stack[(size_t) count++] = note;
        last = note;
    }

    void noteOff (int note)
    {
        remove (note);
        if (count > 0) last = stack[(size_t) count - 1];   // fall back to the note still held underneath
    }

    void allOff() { count = 0; }                           // last stays as it was
    void reset()  { count = 0; last = -1; }

    bool held() const { return count > 0; }
    int  current() const { return last; }

private:
    void remove (int note)
    {
        int w = 0;
        for (int i = 0; i < count; ++i)
            if (stack[(size_t) i] != note) stack[(size_t) w++] = stack[(size_t) i];
        count = w;
    }

    std::array<int, 16> stack {};
    int count = 0;
    int last = -1;
};
