#include "logger.hh"
#include "util/dyom/JoshSesssion.hh"

static DyomJoshSession *sm_Session = nullptr;

/*******************************************************/
void
DrawJoshTimer (char *out, const char *format, int hours, int minutes)
{
    static bool wasTimerRunning = false;

    if (sm_Session && sm_Session->IsRunning ()
        && sm_Session->GetTimeLeft () > 0)
        {
            wasTimerRunning = true;

            hours   = sm_Session->GetTimeLeft () / 60;
            minutes = sm_Session->GetTimeLeft () % 60;

            if (hours >= 100) {
                minutes = hours % 60;
                hours /= 60;
                sprintf (out, "%2dh%02dm", hours, minutes);
                return;
            }
        }
    else if (wasTimerRunning)
        {
            sprintf (out, "~r~00:00");
            return;
        }

    sprintf (out, format, hours, minutes);
}

/*******************************************************/
DyomJoshSession::DyomJoshSession ()
{
    RegisterHooks ({{HOOK_CALL, 0x58EBAF, (void *) DrawJoshTimer}});

    sm_Session = this;

    LoadSessionInfo ();
    LoadPlayedMissions ();
}

/*******************************************************/
void
DyomJoshSession::ReportMissionStart (const std::string &html,
                                     const std::string &url)
{
    std::string name;
    std::string author;

    {
        std::regex  re ("<title>DYOM - \"(.+?)\" by (.+?)</title>");
        std::cmatch cm;
        if (std::regex_search (html.c_str (), cm, re))
            {
            name   = cm[1];
            author = cm[2];

            DyomTranslator::DecodeSpecialChars (name);
            DyomTranslator::DecodeSpecialChars (author);

            FPrintf ("name.txt", "mission/", "%s", name.c_str ());
            FPrintf ("author.txt", "mission/", "%s", author.c_str ());
            }
    }

    {
        std::regex  re ("Last Update</dt><dd>(.+?)</dd>");
        std::cmatch cm;
        if (std::regex_search (html.c_str (), cm, re))
            FPrintf ("date.txt", "mission/", "%s", cm[1].str ().c_str ());
    }

    // The mission played list depends on this url for storing the last played
    // mission. Do not change the format without changing the played list logic
    // first.
    FPrintf ("url.txt", "mission/", "%s", url.c_str ());

    if (sessionFile)
        {
            fprintf (sessionFile, "%02d:%02d | %s | %s | %s\n",
                     GetTimeLeft () / 60, GetTimeLeft () % 60, name.c_str (),
                     author.c_str (), url.c_str ());
            fflush (sessionFile);
        }
    else
        puts ("SESSIONERROR: NO SESSION FILE");
}

/*******************************************************/
void
DyomJoshSession::ReportObjective (const std::string &original,
                                  const std::string &translated)
{
    objectiveTime = time (NULL);

    if (!subtitlesFile)
        subtitlesFile = OpenFile ("subtitles.txt", "a");

    std::string out = std::regex_replace (original, std::regex ("~.+?~"), "");
    out             = std::regex_replace (out, std::regex ("_"), "");
    out             = std::regex_replace (out, std::regex ("\\s+"), " ");

    if (original == translated)
        out = "";

    printf ("%s %s\n", original.c_str (), translated.c_str ());

    fprintf (subtitlesFile, "%s\n", out.c_str ());
    fflush (subtitlesFile);
}

/*******************************************************/
void
DyomJoshSession::BackupObjectiveTexts (std::string texts[100])
{
    auto file = OpenFile ("objectivesBackup.txt", "w", "internal/");

    if (!file)
        return;

    for (int i = 0; i < 100; i++)
        fprintf (file, "%s\n", texts[i].c_str ());

    fclose (file);
}

/*******************************************************/
void
DyomJoshSession::RestoreObjectiveTexts (std::string out[100])
{
    auto file = OpenFile ("objectivesBackup.txt", "r", "internal/");

    if (!file)
        return;

    char buf[101];
    for (int i = 0; i < 100; i++)
        {
            fgets (buf, 101, file);
            buf[strcspn (buf, "\n")] = 0;
            out[i]                   = buf;
        }

    fclose (file);
}

/*******************************************************/
bool
DyomJoshSession::IsRestoringFromCrash ()
{
    if (restoringFromCrash)
        {
            restoringFromCrash = false;
            return true;
        }
    return false;
}

/*******************************************************/
void
DyomJoshSession::ProcessTimer ()
{
    if (active && time (NULL) - startTime > SESSION_DURATION)
        active = false;

    auto &config = DyomRandomizer::GetInstance ()->m_Config;

    // Keybinds
    if (IsKeyUp<VK_F4> (config.IncrementPassCounterKey))
        ReportMissionPass ();

    if (IsKeyUp<VK_F5> (config.IncrementSkipCounterKey))
        ReportMissionSkip ();

    if (IsKeyUp<VK_F6> (config.DecrementPassCounterKey))
        {
            missionsPassed--;
            SaveSessionInfo ();
        }

    if (IsKeyUp<VK_F7> (config.DecrementSkipCounterKey))
        {
            missionsSkipped--;
            SaveSessionInfo ();
        }

    if (objectiveTime != 0 && time (NULL) - objectiveTime >= 5)
        {
            ReportObjective ("", "");
            objectiveTime = 0;
        }
}

/*******************************************************/
void
DyomJoshSession::Reset (bool write)
{
    if (subtitlesFile)
        {
            fclose (subtitlesFile);
            subtitlesFile = nullptr;
        }

    if (sessionFile)
        {
            fclose (sessionFile);
            sessionFile = nullptr;
        }

    subtitlesFile = OpenFile ("subtitles.txt", "w");
    graphFile     = OpenFile ("graph.txt", "w");

    startTime       = time (NULL);
    missionsPassed  = 0;
    missionsSkipped = 0;
    active          = false;

    OpenSessionFile ();

    if (write)
        SaveSessionInfo ();
}

/*******************************************************/
void
DyomJoshSession::OpenSessionFile ()
{
    if (sessionFile)
        {
            fclose (sessionFile);
            sessionFile = nullptr;
        }

    printf ("[SESSION]: Start Time: %llu", startTime);
    sessionFile = OpenFile (GetSessionFileName (startTime), "a", "sessions/");
}

/*******************************************************/
void
DyomJoshSession::LoadSessionInfo ()
{
    FILE *info = OpenFile ("sessionInfo.txt", "r", "internal/");
    if (!info)
        {
            return;
        }

    int read = fscanf (info, "%llu %d %d", &startTime, &missionsPassed,
                       &missionsSkipped);

    fclose (info);

    // Validate file and check if session is still valid.
    if (read == 3 && time (NULL) - startTime < SESSION_DURATION)
        {
            subtitlesFile      = OpenFile ("subtitles.txt", "a");
            graphFile          = OpenFile ("graph.txt", "a");
            active             = true;
            restoringFromCrash = true;
            OpenSessionFile ();
        }
}

/*******************************************************/
void
DyomJoshSession::SaveSessionInfo ()
{
    FPrintf ("timer.html", "",
             "<script>var timeStart = %d;</script><script "
             "type='text/javascript' src='timer.js'></script><div "
             "id=\"timer\"></div>",
             startTime);

    FPrintf ("sessionInfo.txt", "internal/", "%llu %d %d", startTime,
             missionsPassed, missionsSkipped);
    FPrintf ("counter.txt", "", "Missions Passed: %d\nMissions Skipped: %d",
             missionsPassed, missionsSkipped);

    fprintf (graphFile, "%lld %d\n", time (NULL) - startTime,
             missionsPassed - missionsSkipped);
    fflush (graphFile);
}

/*******************************************************/
std::string
DyomJoshSession::GetSessionFileName (time_t start)
{
    char str[256];

    auto tm = std::localtime (&start);
    sprintf (str, "%04d-%02d-%02d-%02d-%02d-%02d.txt", 1900 + tm->tm_year,
             tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    printf ("%s\n", str);
    return str;
}

/*******************************************************/
void
DyomJoshSession::LoadPlayedMissions ()
{
    // There is no need to clear the set, is there? If the user wants to remove
    // any played missions they can restart the game.

    FILE* file = OpenFile ("playedMissions.txt", "r", "internal/");

    if (!file)
        return;

    char buf[256];
    while (fgets (buf, 256, file))
        playedMissions.insert (std::stoi (buf));

    fclose (file);
}

/*******************************************************/
void
DyomJoshSession::SavePlayedMissions ()
{
    FILE* file = OpenFile ("playedMissions.txt", "w", "internal/");

    if (!file)
        return;

    for (auto &mission : playedMissions)
        fprintf (file, "%d\n", mission);

    fclose (file);
}

/*******************************************************/
bool
DyomJoshSession::ShouldSkipMission (const std::string &url)
{
    return playedMissions.count (std::stoi (url.substr (5)));
}

/*******************************************************/
void
DyomJoshSession::AddLastMissionToPlayedList ()
{
    FILE *lastMissionFile = OpenFile ("url.txt", "r", "mission/");
    if (!lastMissionFile)
        {
            Logger::GetLogger ()->LogMessage (
                "Failed to add last mission to played list. Somehow there is "
                "no url.txt!");
            return;
        }

    char buf[256];
    fgets(buf, 256, lastMissionFile);
    fclose(lastMissionFile);

    playedMissions.insert(std::stoi(buf+5));
    SavePlayedMissions();
}

/*******************************************************/
void
DyomJoshSession::ReportMissionUnplayable (const std::string &url)
{
    playedMissions.insert (std::stoi (url.substr (5)));
    SavePlayedMissions ();
}
