// Included by ShadowCameraTests after the production translation unit.
static void CheckSunShadowTimeline()
{
    namespace sun = SunShadowTimeline;
    for (int fps : {120,240,250})
    {
        sun::History history{};
        double previousAngle = 0;
        unsigned changes = 0;
        int previousProducer = -1;
        for (int frame = 0; frame <= fps*3; ++frame)
        {
            const double now = double(frame)/fps;
            const int producer = int(std::floor((now+1e-9)/0.08));
            if (producer != previousProducer) { ++changes; previousProducer = producer; }
            const double angle = producer*0.08*0.003;
            const float native[] = {float(std::cos(angle)),float(std::sin(angle)),0};
            float output[3]{};
            const bool ready = sun::Sample(history,native,now,frame,output);
            assert(history.count == std::min(changes,sun::kCapacity)); // held inputs never add keys
            if (!ready) { assert(producer == 0); continue; }
            const double actual = std::atan2(double(output[1]),output[0]);
            unsigned next = 1;
            while (next+1 < history.count && history.keys[next].time < history.presentedTime) ++next;
            const auto& left = history.keys[next-1]; const auto& right = history.keys[next];
            const double alpha = (history.presentedTime-left.time)/(right.time-left.time);
            const double expected = (1-alpha)*std::atan2(left.direction[1],left.direction[0])+
                alpha*std::atan2(right.direction[1],right.direction[0]);
            assert(std::fabs(actual-expected) < 1e-8);
            assert(actual >= previousAngle-1e-8);
            assert(actual-previousAngle <= 0.003/fps*1.5+1e-8);
            assert(std::fabs(double(output[0])*output[0]+double(output[1])*output[1]-1) < 2e-7);
            if (now > 0.3) assert(std::fabs(actual-0.003*(now-sun::kDelaySeconds)) < 0.003/fps+1e-8);
            float repeated[3]{};
            assert(sun::Sample(history,native,now+0.0001,frame,repeated));
            assert(!memcmp(output,repeated,sizeof(output)));
            previousAngle = actual;
        }
        const unsigned count = history.count;
        const auto latest = history.keys[count-1];
        const float held[] = {float(latest.direction[0]),float(latest.direction[1]),float(latest.direction[2])};
        float output[3]{};
        for (int frame = 1; frame <= fps; ++frame)
            assert(sun::Sample(history,held,3.0+double(frame)/fps,3*fps+frame,output));
        assert(history.count == count); // a held sun clamps; never extrapolate
        assert(std::fabs(std::atan2(double(output[1]),output[0])-std::atan2(latest.direction[1],latest.direction[0])) < 1e-8);
        assert(!sun::Sample(history,held,5.0,5*fps,output) && history.count == 1); // pause/gap
        assert(!sun::Sample(history,held,5.01,0,output) && history.count == 1); // generation rollback
        const float cut[] = {0,1,0};
        assert(!sun::Sample(history,cut,5.02,1,output) && history.count == 1);
        const float invalid[] = {0,0,0};
        assert(!sun::Sample(history,invalid,5.03,2,output) && history.count == 0);
    }
    sun::History history{};
    const float a[] = {1,0,0}, b[] = {float(std::cos(0.01)),float(std::sin(0.01)),0};
    float output[3]{};
    assert(!sun::Sample(history,a,10,1,output));
    assert(sun::Sample(history,b,10.08,2,output));
    assert(!sun::Sample(history,a,10.081,2,output) && history.ambiguous);
    assert(!sun::Sample(history,b,10.082,2,output)); // never revive an ambiguous generation
    assert(sun::Sample(history,b,10.09,3,output));
    assert(!sun::Sample(history,b,9.0,4,output) && history.count == 1); // time rollback
    const float nonfinite[] = {std::numeric_limits<float>::quiet_NaN(),0,0};
    assert(!sun::Sample(history,nonfinite,9.1,5,output) && history.count == 0);
}

static void __fastcall ThrowSunBuilder(uintptr_t, uintptr_t, uintptr_t)
{ RaiseException(0xe0432401,0,0,nullptr); }
static bool CatchSunBuilderException(uintptr_t owner, uintptr_t frame, const float* native, const float* presented)
{
    __try { InvokeShadowBuilderWithSun(owner,frame,0x1234,native,presented,true); }
    __except(GetExceptionCode() == 0xe0432401 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
