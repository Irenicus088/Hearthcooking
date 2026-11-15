// Hearthcooking's kitchen, played by script: a careful cook should make Masterwork, a careless one should burn it.
#include "../src/sim/Kitchen.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hc;

static int g_fail = 0;
#define CHECK(c, ...)                                   \
	do {                                                \
		if (!(c)) {                                     \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                   \
			std::printf("\n");                          \
			++g_fail;                                   \
		}                                               \
	} while (0)

static void Print(const char* a_name, const Result& r)
{
	std::printf("%-28s %-10s q %.2f", a_name, TierName(r.tier), r.quality);
	for (int i = 0; i < r.count; ++i) {
		std::printf(" | %s: %s (%.2f)", r.lines[i].label, r.lines[i].verdict, r.lines[i].score);
	}
	if (r.tier == Tier::Burnt) {
		std::printf(" | %s", r.burntWhy);
	}
	std::printf("\n");
}

static void TestFire()
{
	Fire f;
	f.Light(2);
	CHECK(std::abs(f.Heat() - 0.6) < 0.01, "two logs give 0.6, got %.3f", f.Heat());
	for (int i = 0; i < 70 * 60; ++i) {
		f.Step(1.0 / 60);
	}
	CHECK(f.Heat() < 0.45 && f.Heat() > 0.3, "after 70 s two logs have burnt down to %.3f", f.Heat());
	f.AddLog();
	for (int i = 0; i < 20 * 60; ++i) {
		f.Step(1.0 / 60);
	}
	CHECK(f.Heat() > 0.55, "a fresh log brings it back up: %.3f", f.Heat());
	Fire g;
	g.Light(1);
	for (int i = 0; i < 200 * 60; ++i) {
		g.Step(1.0 / 60);
	}
	CHECK(g.Logs() == 0 && g.Heat() < 0.05, "a lone log burns out (%d logs, heat %.3f)", g.Logs(), g.Heat());
}

static void TestCutting()
{
	Cutting c;
	c.target = 1.5;
	for (double x = c.x1 - 1.5; x > 0.5; x -= 1.5) {
		c.Chop(x);
	}
	c.Finish();
	const double good = c.Score();
	CHECK(good > 0.85, "even 1.5 cuts score %.2f", good);
	Cutting d;
	d.target = 1.5;
	d.Chop(4.0);
	d.Finish();
	CHECK(d.Score() < 0.45, "two big hunks score %.2f", d.Score());
	Cutting e;
	e.Finish();
	CHECK(e.Score() < 0.1, "whole scores %.2f", e.Score());
	Cutting r;
	r.target = 1.5;
	const double xs[] = { 8.2, 7.9, 5.5, 5.1, 3.0, 1.0 };
	for (double x : xs) {
		r.Chop(x);
	}
	r.Finish();
	CHECK(r.Score() < good - 0.2, "ragged cuts score %.2f", r.Score());
	std::printf("cuts: even %.2f, ragged %.2f, two hunks %.2f\n", good, r.Score(), d.Score());
}

// Lays 3 ingredients' pieces on the board's lanes and tips the board into the pot, held at a_y.
static void Tip(Chunks& a_ch, double a_y, int& a_in, int& a_lost)
{
	unsigned seed = 1;
	for (int lane = 0; lane < 3; ++lane) {
		for (double x = -4.0; x <= 4.0; x += 1.6) {
			a_ch.Add(lane, { x, layout::kLaneY[lane], layout::kBoardH * 0.5 + 0.6 }, { 1.4, 1.2, 1.0 }, seed++);
		}
	}
	Chunks::Board prev = BoardPose(0.0, a_y);
	const double  dt = 1.0 / 60;
	for (int i = 0; i < 6 * 60; ++i) {
		const double  t = std::fmin(1.0, i * dt / 2.0);  // two seconds to tip it right over
		Chunks::Board b = BoardPose(t, a_y);
		b.v = (b.c - prev.c) * (1.0 / dt);
		prev = b;
		a_ch.Step(dt, b, 1.0);
	}
	a_in = a_ch.CountIn(Chunks::State::Pot);
	a_lost = a_ch.CountIn(Chunks::State::Lost);
}

static void TestTipping()
{
	Chunks a;
	int    in = 0, lost = 0;
	Tip(a, layout::kPotY, in, lost);
	std::printf("tipped over the pot: %d in, %d lost, %d still going\n", in, lost, a.Count() - in - lost);
	CHECK(in >= a.Count() - 1, "held over the pot, nearly everything goes in (%d of %d)", in, a.Count());
	Chunks b;
	Tip(b, layout::kPotY - 7.5, in, lost);
	std::printf("tipped off to one side: %d in, %d lost, %d still going\n", in, lost, b.Count() - in - lost);
	CHECK(lost >= 4, "held off to one side, some go astray (%d lost)", lost);
}

static Result Stew(bool a_careful, int a_logs, double a_salt, double a_serveAt)
{
	Fire f;
	f.Light(a_logs);
	Pot  pot(true);
	const double dt = 1.0 / 60;
	double       cuts[2];
	for (int k = 0; k < 2; ++k) {
		Cutting c;
		c.target = 1.5;
		for (double x = c.x1 - (a_careful ? 1.5 : 3.0); x > 0.5; x -= a_careful ? 1.5 : 3.0) {
			c.Chop(x + (a_careful ? 0.0 : (k ? 0.7 : -0.6)));
		}
		c.Finish();
		cuts[k] = c.Score();
	}
	pot.AddPieces(0, 12);
	pot.Seasoning().amount = a_salt;
	for (double t = 0; t < 400 && pot.Cooked() < a_serveAt; t += dt) {
		if (a_careful) {
			pot.Stir(3.5 * dt);  // round and round, about half a turn a second
			if (f.Target() < 0.45 && f.Logs() < 4) {
				f.AddLog();
			}
		}
		f.Step(dt);
		pot.Step(dt, f.Heat(), true);
	}
	return pot.Score(cuts, 2, 12, a_careful ? 0 : 3);
}

static void TestPot()
{
	const Result good = Stew(true, 2, 1.0, 1.05);
	Print("stew, careful", good);
	CHECK(good.tier == Tier::Masterwork, "a careful stew is Masterwork");
	const Result hot = Stew(false, 4, 0.0, 1.05);
	Print("stew, roaring and left", hot);
	CHECK(hot.tier == Tier::Burnt, "a roaring fire and no stirring burns it");
	const Result raw = Stew(true, 2, 1.0, 0.5);
	Print("stew, served early", raw);
	CHECK(raw.tier == Tier::Plain, "served half-cooked: Plain");
	const Result salty = Stew(true, 2, 3.0, 1.05);
	Print("stew, over-salted", salty);
	CHECK(salty.quality < good.quality, "too much salt costs something");
}

static Result Roast(double a_turn, int a_logs, double a_until)
{
	Fire f;
	f.Light(a_logs);
	Spit s(true);
	s.Seasoning().amount = 1.0;
	const double dt = 1.0 / 60;
	for (double t = 0; t < 300 && s.Mean() < a_until; t += dt) {
		s.Turn(a_turn * dt);
		if (a_turn > 0 && f.Target() < 0.45) {
			f.AddLog();
		}
		f.Step(dt);
		s.Step(dt, f.Heat(), true);
	}
	return s.Score();
}

static void TestSpit()
{
	const Result good = Roast(3.0, 2, 1.05);
	Print("roast, turned steadily", good);
	CHECK(good.tier == Tier::Masterwork, "turned steadily: Masterwork");
	const Result left = Roast(0.0, 2, 1.05);
	Print("roast, never turned", left);
	CHECK(left.tier == Tier::Burnt, "never turned: one side burns");
	const Result early = Roast(3.0, 2, 0.6);
	Print("roast, off too soon", early);
	CHECK(early.tier == Tier::Plain || early.tier == Tier::Hearty, "off too soon: not Masterwork");
}

static Result Bake(bool a_pie, double a_gap, int a_strokes, double a_until, bool a_peek)
{
	Fire f(9.0);
	f.Light(2);
	Oven o(a_pie);
	double now = 0;
	for (int k = 0; k < a_strokes; ++k) {
		now += a_gap;
		o.Knead(5.0, now);
	}
	if (a_pie) {
		for (int k = 0; k < 16; ++k) {
			const double a = k * 0.785, r = k < 8 ? 0.3 : 0.8;
			o.Fill(std::cos(a) * r, std::sin(a) * r);
		}
		for (int k = 0; k < 24; ++k) {
			o.Crimp(k * 0.262 + 0.05);
		}
	}
	const double dt = 1.0 / 60;
	for (double t = 0; t < 20; t += dt) {  // the oven heats while the dough is worked
		f.Step(dt);
		o.Step(dt, f.Heat());
	}
	o.Load();
	o.SetDoor(false);
	for (double t = 0; t < 400 && o.Brown() < a_until; t += dt) {
		if (f.Target() < 0.45) {
			f.AddLog();
		}
		if (a_peek) {
			o.SetDoor(std::fmod(t, 6.0) < 2.0);
		}
		f.Step(dt);
		o.Step(dt, f.Heat());
	}
	o.Unload();
	return o.Score();
}

static void TestOven()
{
	const Result pie = Bake(true, 0.7, 12, 1.0, false);
	Print("pie, careful", pie);
	CHECK(pie.tier == Tier::Masterwork, "a careful pie is Masterwork");
	const Result bread = Bake(false, 0.7, 12, 1.0, false);
	Print("bread, careful", bread);
	CHECK(bread.tier == Tier::Masterwork, "careful bread is Masterwork");
	const Result tough = Bake(false, 0.15, 30, 1.0, false);
	Print("bread, pounded", tough);
	CHECK(tough.tier != Tier::Masterwork, "pounded too fast and too long: not Masterwork");
	const Result burnt = Bake(false, 0.7, 12, 1.7, false);
	Print("bread, forgotten", burnt);
	CHECK(burnt.tier == Tier::Burnt, "forgotten in the oven: Burnt");
	const Result peeky = Bake(false, 0.7, 12, 1.0, true);
	Print("bread, always peeking", peeky);
	CHECK(peeky.quality < bread.quality, "opening the door all the time costs heat");
}

int main()
{
	TestFire();
	TestCutting();
	TestTipping();
	TestPot();
	TestSpit();
	TestOven();
	if (g_fail) {
		std::printf("%d check(s) failed\n", g_fail);
		return 1;
	}
	std::printf("all good\n");
	return 0;
}
